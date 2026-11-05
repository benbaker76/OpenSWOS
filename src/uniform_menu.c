/* uniform_menu.c -- the UNIFORM screen: the kit each side wears for the next match.
 * SDL only. docs/UNIFORM.md.
 *
 * The screen is SWOS 2020's (swos-2020/src/menus/selectUniform.mnu, handlers in
 * game.cpp), run from JSON like its online menus (docs/MENUS.md section 10); the
 * UNIFORM button on the PLAY MATCH screen opens it. What it does with the result is
 * OpenSWOS's own:
 *
 *   - A CHOICE IS KEPT BESIDE THE TEAM, never written into it. SWOS 2020 wrote the kit
 *     into the team's file, which in a career is the team the save keeps. Here a choice
 *     lasts one match: SetupPlayers forgets it, kick-off dresses the side from it
 *     (Uniform_Kits, called by InitializeInGameTeamsAndStartGame).
 *
 *   - A CHOSEN KIT IS WORN, AS IN SWOS 2020 (g_CustomUniform; ReportBot 0028, since
 *     2026-10-07). When either side chose, and in every live online match, both sides
 *     wear their first kit and SWOS's rule for kits that are too alike is not asked
 *     (SetInGameTeamsFirstKits). A chosen kit replaces both of that side's kits, so
 *     "first" is the choice, and online that needs nothing sent but the team. Until
 *     then the rule still decided, which overruled choices that were fine. A match
 *     where nobody chose is dressed by the rule as it always was.
 *
 *   - THE SCREEN SHOWS WHAT WILL BE WORN. A side nobody has touched is drawn in the kit
 *     that rule would give it against the other side as it stands, so opening the
 *     screen and leaving changes nothing, and the help line says so when the rule will
 *     overrule a choice.
 *
 * The drawings are kit_art.c's, in the Amiga's art or the Jaguar's ([graphics] kit_art);
 * the Jaguar's are taller, so the screen is laid out for the art in use. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "uniform.h"
#include "kit_art.h"
#include "display.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "swos2020_menus.h"
#include "team_colors.h"

extern TeamGame  _topTeamInGame, _bottomTeamInGame;
extern uint16_t  _kTeamColorsTable[KIT_COLOURS];
extern uint16_t  _controlMask;
extern uint8_t   _currentMenu[];
extern void      SetExitMenuFlag(void);
extern void      SetInGameTeamsPrimaryColors(TeamFile *team1File, TeamFile *team2File);
extern void      SetInGameTeamsFirstKits(TeamFile *team1File, TeamFile *team2File);
extern uint8_t   _teamKitsSimilarity[4];

enum {
    kLeft, kRight, kSides,
    kSideEntries = SelectUniformMenu_RightShirtType0 - SelectUniformMenu_LeftShirtType0,
    kFireCombos = 0x3C1,        /* fire, and fire with each direction (MenuProc) */
    kFireLeft = 0x41, kFireRight = 0x81, kFireUp = 0x101, kFireDown = 0x201,
    kBoxColour = 7,             /* grey: what the kit drawings sit on */
    kPickedColour = 14,         /* green: the shirt type in use */
};

/* ---- the choices: one per team of the match being set up ---- */

static struct Choice {
    const TeamFile *team;
    KitLook         look;
    int32_t         chosen;
} s_choice[kSides];

void Uniform_Clear(void)
{
    memset(s_choice, 0, sizeof s_choice);
}

static struct Choice *choice_of(const TeamFile *team)
{
    int32_t i;
    for (i = 0; i < kSides; i++)
        if (s_choice[i].team == team && s_choice[i].chosen)
            return &s_choice[i];
    return NULL;
}

static void effective(const TeamFile *team, TeamFile *out)
{
    const struct Choice *c = choice_of(team);
    *out = *team;
    if (c) {
        KitArt_ToTeam(&c->look, 0, out);
        KitArt_ToTeam(&c->look, 1, out);
    }
}

int32_t Uniform_Kits(const TeamFile *team1, const TeamFile *team2, TeamFile *kit1, TeamFile *kit2)
{
    int32_t chosen = choice_of(team1) || choice_of(team2);

    effective(team1, kit1);
    effective(team2, kit2);
    if (chosen)
        fprintf(stderr, "[uniform] kick-off: %.16s %s, %.16s %s; the game changes neither\n",
                (const char *)team1->teamName, choice_of(team1) ? "in a chosen kit" : "in its first kit",
                (const char *)team2->teamName, choice_of(team2) ? "in a chosen kit" : "in its first kit");
    return chosen;
}

void Uniform_Choose(const TeamFile *team, const KitLook *look)
{
    struct Choice *c = choice_of(team);
    int32_t i;

    for (i = 0; i < kSides && !c; i++)
        if (!s_choice[i].chosen)
            c = &s_choice[i];
    if (!c)
        c = &s_choice[0];
    c->team = team;
    c->look = *look;
    c->chosen = 1;
}

int32_t Uniform_Take(const TeamFile *team, KitLook *out)
{
    struct Choice *c = choice_of(team);
    if (!c)
        return 0;
    *out = c->look;
    c->chosen = 0;
    return 1;
}

/* ---- the screen ---- */

static struct {
    TeamFileEx *team[kSides];
    KitLook     shown[kSides];      /* what each side is drawn in */
    int32_t     editable[kSides];
    int32_t     leftIsTop;
    int32_t     overruled;          /* SWOS's rule will change a chosen kit */
    int32_t     alike;              /* the two kits, kept as chosen, are too alike by that rule */
    int32_t     laidOut;            /* this unpack of the menu has been arranged */
} s_menu;

static int32_t s_online = -1;       /* -1 offline; else whether the other side is known */
static int32_t s_onlineTop;

void UniformMenu_SetOnline(int32_t otherKnown, int32_t localIsTop)
{
    s_online = otherKnown;
    s_onlineTop = localIsTop;
}

static int32_t same_look(const KitLook *a, const KitLook *b)
{
    return a->type == b->type && a->stripes == b->stripes && a->shorts == b->shorts &&
           a->socks == b->socks && (a->type == SHIRT_ORDINARY || a->basic == b->basic);
}

static uint8_t colour_of(int16_t paletteEntry)
{
    uint8_t c;
    for (c = 0; c < KIT_COLOURS; c++)
        if (_kTeamColorsTable[c] == (uint16_t)paletteEntry)
            return c;
    return 0;
}

/* What a side kicks off in, read back from the in-game team SWOS's rule filled. */
static void worn(const TeamGame *game, const TeamFile *kits, KitLook *out)
{
    KitLook pr, sec;

    const uint8_t *w = TeamColors_Worn(game);  /* (the colours given: purple and maroon are one entry) */
    out->type    = (uint8_t)(game->prShirtType & 3);
    out->stripes = w ? w[1] : colour_of(game->prShirtCol);
    out->basic   = w ? (out->type != SHIRT_ORDINARY ? w[2] : w[1]) : colour_of(game->prStripesCol);
    out->shorts  = w ? w[3] : colour_of(game->prShortsCol);
    out->socks   = w ? w[4] : colour_of(game->prSocksCol);
    /* A plain shirt has no second colour to read back: keep the kit's own, so that
     * changing the type afterwards starts from the team's colours. */
    KitArt_FromTeam(kits, 0, &pr);
    KitArt_FromTeam(kits, 1, &sec);
    if (same_look(out, &pr))       out->basic = pr.basic;
    else if (same_look(out, &sec)) out->basic = sec.basic;
}

/* Run SWOS's kit rule on the two sides as they stand, and put back what it wrote. */
static void refresh(void)
{
    static TeamGame keepTop, keepBottom;
    TeamFile kit[kSides];
    KitLook  will[kSides];
    int32_t  top = s_menu.leftIsTop ? kLeft : kRight, side;

    s_menu.overruled = 0;
    s_menu.alike = 0;
    if (!s_menu.team[kRight]) {             /* nobody to clash with yet */
        const struct Choice *c = choice_of(&s_menu.team[kLeft]->team);
        if (c) s_menu.shown[kLeft] = c->look;
        else   KitArt_FromTeam(&s_menu.team[kLeft]->team, 0, &s_menu.shown[kLeft]);
        return;
    }
    effective(&s_menu.team[kLeft]->team, &kit[kLeft]);
    effective(&s_menu.team[kRight]->team, &kit[kRight]);
    keepTop = _topTeamInGame;
    keepBottom = _bottomTeamInGame;
    /* As kick-off will: a chosen kit is worn as chosen, and so is every kit of an
     * online match (game_init.c). Then the screen only says when the two are alike. */
    if (s_online >= 0 || choice_of(&s_menu.team[kLeft]->team) || choice_of(&s_menu.team[kRight]->team)) {
        SetInGameTeamsFirstKits(&kit[top], &kit[1 - top]);
        s_menu.alike = _teamKitsSimilarity[0] >= 11;    /* the rule's own measure of "too alike" */
    } else {
        SetInGameTeamsPrimaryColors(&kit[top], &kit[1 - top]);
    }
    worn(&_topTeamInGame, &kit[top], &will[top]);
    worn(&_bottomTeamInGame, &kit[1 - top], &will[1 - top]);
    _topTeamInGame = keepTop;
    _bottomTeamInGame = keepBottom;

    for (side = 0; side < kSides; side++) {
        const struct Choice *c = choice_of(&s_menu.team[side]->team);
        if (c) {
            s_menu.shown[side] = c->look;
            if (!same_look(&c->look, &will[side]))
                s_menu.overruled = 1;
        } else {
            s_menu.shown[side] = will[side];
        }
    }
}

/* A side's kit is being changed: from here on it is that side's choice. */
static KitLook *choose(int32_t side)
{
    const TeamFile *team = &s_menu.team[side]->team;

    if (!choice_of(team))
        Uniform_Choose(team, &s_menu.shown[side]);
    return &choice_of(team)->look;
}

static MenuEntry *entry_of(int32_t side, int32_t leftOrdinal)
{
    return CalcMenuEntryAddress((uint16_t)(leftOrdinal + side * kSideEntries));
}

/* ---- drawing: each kit box is an ordinary entry, drawn on by its after-draw hook ---- */

static int32_t side_of(const MenuEntry *e) { return e->ordinal >= SelectUniformMenu_RightShirtType0; }

static void draw_part(MenuEntry *e, int32_t part)
{
    KitArt_Draw(part, &s_menu.shown[side_of(e)], e->x, e->y, e->dwWidth, e->dwHeight);
}

/* The shirt, and above it the little player of the stadium screen wearing the lot. */
static void draw_shirt(MenuEntry *e)
{
    int32_t w, h;
    draw_part(e, KIT_ART_SHIRT);
    KitArt_Size(KIT_ART_PLAYER, &w, &h);
    KitArt_Draw(KIT_ART_PLAYER, &s_menu.shown[side_of(e)], e->x, e->y - h - 3, e->dwWidth, h);
}
static void draw_shorts(MenuEntry *e) { draw_part(e, KIT_ART_SHORTS); }
static void draw_socks(MenuEntry *e)  { draw_part(e, KIT_ART_SOCKS); }

static void draw_icon(MenuEntry *e)
{
    KitLook icon = s_menu.shown[side_of(e)];
    icon.type = (uint8_t)((e->ordinal - SelectUniformMenu_LeftShirtType0) % kSideEntries);
    KitArt_Draw(KIT_ART_ICON, &icon, e->x, e->y, e->dwWidth, e->dwHeight);
}

static void box(MenuEntry *e, MenuEntryFn draw)
{
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = kBoxColour;
    e->content = 0;
    e->afterDrawCb = draw;
}

/* ---- layout ---- */

/* Rows of one side, as offsets from the .mnu's own (which suit the Amiga's art): the
 * Jaguar's shirt is 12 px taller than the Amiga's, its shorts 7 and its socks 10, and
 * its icons 4. Everything below the shirt moves down by what is above it. */
static void lay_out(void)
{
    static const int16_t k_iconY[4] = { -8, -3, 2, 7 };     /* 24 px apart, not 19 */
    int32_t side, i;

    if (s_menu.laidOut)
        return;
    s_menu.laidOut = 1;

    for (side = 0; side < kSides; side++) {
        MenuEntry *shirt  = entry_of(side, SelectUniformMenu_LeftShirts);
        MenuEntry *shorts = entry_of(side, SelectUniformMenu_LeftShorts);
        MenuEntry *socks  = entry_of(side, SelectUniformMenu_LeftSocks);

        box(shirt, draw_shirt);
        box(shorts, draw_shorts);
        box(socks, draw_socks);
        shirt->controlMask = shorts->controlMask = socks->controlMask = kFireCombos;
        for (i = 0; i < 4; i++)
            box(entry_of(side, SelectUniformMenu_LeftShirtType0 + i), draw_icon);

        if (KitArt_Jaguar()) {
            shirt->y -= 12;     shirt->dwHeight += 12;
            shorts->y += 0;     shorts->dwHeight += 7;
            socks->y += 7;      socks->dwHeight += 10;
            for (i = 0; i < 4; i++) {
                MenuEntry *icon = entry_of(side, SelectUniformMenu_LeftShirtType0 + i);
                icon->y += k_iconY[i];
                icon->dwHeight += 4;
            }
            for (i = SelectUniformMenu_LeftStripesColor_Dec; i <= SelectUniformMenu_LeftSocksColor_Inc; i++) {
                static const int16_t k_rowY[4] = { -8, -3, 3, 14 };
                entry_of(side, i)->y += k_rowY[(i - SelectUniformMenu_LeftStripesColor_Dec) % 4];
            }
            entry_of(side, SelectUniformMenu_LeftSwitch)->y += 19;
            entry_of(side, SelectUniformMenu_LeftReset)->y += 19;
        }
    }
    if (KitArt_Jaguar())
        CalcMenuEntryAddress(SelectUniformMenu_HelpMsg)->y += 10;

    /* A side that cannot be changed keeps its drawings and loses its buttons, and the
     * links that led to them. */
    for (side = 0; side < kSides; side++) {
        if (s_menu.team[side] && s_menu.editable[side])
            continue;
        for (i = SelectUniformMenu_LeftShirtType0; i <= SelectUniformMenu_LeftReset; i++) {
            MenuEntry *e = entry_of(side, i);
            int32_t drawing = i <= SelectUniformMenu_LeftShirtType3 ||
                              (i >= SelectUniformMenu_LeftShirts && i <= SelectUniformMenu_LeftSocks);
            if (!s_menu.team[side] || !drawing)
                e->isInvisible = 1;
            e->isDisabled = 1;
        }
        CalcMenuEntryAddress((uint16_t)(side ? SelectUniformMenu_RightTeamName
                                             : SelectUniformMenu_LeftTeamName))->isInvisible = !s_menu.team[side];
        for (i = SelectUniformMenu_LeftShirtType0; i <= SelectUniformMenu_LeftReset; i++) {
            MenuEntry *e = entry_of(1 - side, i);
            if (side == kRight) { if (e->rightEntry >= SelectUniformMenu_RightShirtType0) e->rightEntry = -1; }
            else if (e->leftEntry >= 0 && e->leftEntry < SelectUniformMenu_RightShirtType0) e->leftEntry = -1;
        }
    }
    if (!s_menu.editable[kLeft] || !s_menu.team[kLeft])
        CalcMenuEntryAddress(SelectUniformMenu_Ok)->upEntry = SelectUniformMenu_RightReset;
}

static uint16_t name_colour(const TeamFileEx *team)
{
    return team->team.teamControls == 1 ? 10 : 13;     /* a computer's team red, a player's blue */
}

/* ---- menu handlers ---- */

static void selectUniformMenuOnInit(void)
{
    s_menu.laidOut = 0;
    if (!s_menu.team[kLeft]) {              /* shown without teams (--menu=): nothing to dress */
        SetExitMenuFlag();
        return;
    }
    refresh();
}

static void selectUniformMenuOnDraw(void)
{
    static char help[72];
    MenuEntry *e = CalcMenuEntryAddress(SelectUniformMenu_HelpMsg);
    int32_t at = ((Menu *)_currentMenu)->selectedEntry ? ((Menu *)_currentMenu)->selectedEntry->ordinal : -1;
    int32_t side, i, on;
    const char *text = "CHANGE COLOUR BY SELECTING - OR +";

    if (!s_menu.team[kLeft])
        return;
    KitArt_Lend();
    lay_out();
    for (side = 0; side < kSides; side++) {
        MenuEntry *name = CalcMenuEntryAddress((uint16_t)(side ? SelectUniformMenu_RightTeamName
                                                               : SelectUniformMenu_LeftTeamName));
        if (!s_menu.team[side])
            continue;
        name->fg = (uintptr_t)s_menu.team[side]->team.teamName;
        name->bgColor = name_colour(s_menu.team[side]);
        for (i = 0; i < 4; i++)         /* the shirt type in use, picked out */
            entry_of(side, SelectUniformMenu_LeftShirtType0 + i)->bgColor =
                s_menu.shown[side].type == i ? kPickedColour : kBoxColour;
    }

    on = at >= SelectUniformMenu_RightShirtType0 ? at - kSideEntries : at;
    if (on == SelectUniformMenu_LeftShirts)
        text = "HOLD FIRE AND PRESS LEFT/RIGHT OR UP/DOWN TO CHANGE COLOUR";
    else if (on == SelectUniformMenu_LeftShorts || on == SelectUniformMenu_LeftSocks)
        text = "HOLD FIRE AND PRESS LEFT/RIGHT TO CHANGE COLOUR";
    else if (on == SelectUniformMenu_LeftSwitch)
        text = "CHANGE TO THE TEAM'S OTHER KIT";
    else if (on == SelectUniformMenu_LeftReset)
        text = "BACK TO THE KIT THE GAME WOULD PICK";
    else if (on >= SelectUniformMenu_LeftShirtType0 && on <= SelectUniformMenu_LeftShirtType3)
        text = "SELECT SHIRT TYPE";
    else if (at == SelectUniformMenu_Ok)
        text = "";
    if (s_menu.overruled)
        text = "THESE KITS ARE TOO ALIKE: THE GAME WILL CHANGE ONE";
    else if (s_menu.alike)
        text = "THESE KITS ARE ALIKE - THE GAME WILL NOT CHANGE THEM";
    snprintf(help, sizeof help, "%s", text);
    e->fg = (uintptr_t)help;
}

static void selectUniformMenuOnReturn(void) {}

static void selectUniformMenu_OkOnSelect(void)
{
    int32_t side;
    for (side = 0; side < kSides; side++)
        if (s_menu.team[side])
            fprintf(stderr, "[uniform] %.16s: %s, type %d colours %d/%d/%d/%d\n",
                    (const char *)s_menu.team[side]->team.teamName,
                    choice_of(&s_menu.team[side]->team) ? "chosen" : "the game's pick",
                    s_menu.shown[side].type, _kTeamColorsTable[s_menu.shown[side].stripes],
                    _kTeamColorsTable[s_menu.shown[side].type == SHIRT_ORDINARY ? s_menu.shown[side].stripes
                                                                                : s_menu.shown[side].basic],
                    _kTeamColorsTable[s_menu.shown[side].shorts], _kTeamColorsTable[s_menu.shown[side].socks]);
    SetExitMenuFlag();
}

/* ---- the buttons, for whichever side the selected entry is on ---- */

static int32_t selected_side(void)
{
    const MenuEntry *e = ((Menu *)_currentMenu)->selectedEntry;
    int32_t side = e && e->ordinal >= SelectUniformMenu_RightShirtType0 && e->ordinal <= SelectUniformMenu_RightReset;
    return s_menu.team[side] && s_menu.editable[side] ? side : -1;
}

static void step(uint8_t *colour, int32_t by)
{
    *colour = (uint8_t)((*colour + KIT_COLOURS + by) % KIT_COLOURS);
}

enum { kStripes, kBasic, kShorts, kSocks };

static void change(int32_t what, int32_t by)
{
    int32_t side = selected_side();
    KitLook *look;

    if (side < 0)
        return;
    look = choose(side);
    switch (what) {
    case kStripes: step(&look->stripes, by); break;
    case kBasic:   step(&look->basic, by);   break;
    case kShorts:  step(&look->shorts, by);  break;
    case kSocks:   step(&look->socks, by);   break;
    }
    refresh();
}

static void shirt_type(int32_t type)
{
    int32_t side = selected_side();
    if (side < 0)
        return;
    choose(side)->type = (uint8_t)type;
    refresh();
}

static void type0(void) { shirt_type(0); }
static void type1(void) { shirt_type(1); }
static void type2(void) { shirt_type(2); }
static void type3(void) { shirt_type(3); }
static void stripesDec(void) { change(kStripes, -1); }
static void stripesInc(void) { change(kStripes, 1); }
static void basicDec(void)   { change(kBasic, -1); }
static void basicInc(void)   { change(kBasic, 1); }
static void shortsDec(void)  { change(kShorts, -1); }
static void shortsInc(void)  { change(kShorts, 1); }
static void socksDec(void)   { change(kSocks, -1); }
static void socksInc(void)   { change(kSocks, 1); }

/* On the drawings themselves: FIRE with a direction, as on the EDIT TEAM screen
 * (EditTeamsChangeShirtsPrimary) -- left/right the shirt's colour, up/down its second,
 * which a plain shirt does not have, so there they change the one it has. */
static void on_shirt(void)
{
    int32_t side = selected_side();
    int32_t second = side >= 0 && s_menu.shown[side].type != SHIRT_ORDINARY ? kBasic : kStripes;

    if      (_controlMask == kFireLeft)  change(kStripes, -1);
    else if (_controlMask == kFireRight) change(kStripes, 1);
    else if (_controlMask == kFireUp)    change(second, -1);
    else if (_controlMask == kFireDown)  change(second, 1);
}

static void on_shorts(void)
{
    if      (_controlMask == kFireLeft)  change(kShorts, -1);
    else if (_controlMask == kFireRight) change(kShorts, 1);
}

static void on_socks(void)
{
    if      (_controlMask == kFireLeft)  change(kSocks, -1);
    else if (_controlMask == kFireRight) change(kSocks, 1);
}

/* SWITCH: the team's other kit -- its second if it is in its first, else its first. */
static void on_switch(void)
{
    int32_t side = selected_side();
    KitLook pr, sec, *look;

    if (side < 0)
        return;
    KitArt_FromTeam(&s_menu.team[side]->team, 0, &pr);
    KitArt_FromTeam(&s_menu.team[side]->team, 1, &sec);
    look = choose(side);
    *look = same_look(look, &pr) ? sec : pr;
    refresh();
}

/* RESET: nobody has chosen for this side; it is the game's to dress again. */
static void on_reset(void)
{
    int32_t side = selected_side();
    struct Choice *c;

    if (side < 0)
        return;
    c = choice_of(&s_menu.team[side]->team);
    if (c)
        c->chosen = 0;
    refresh();
}

#define HANDLER(fn)            { #fn, (void *)fn, SYM_FUNC }
#define HANDLER_AS(name, fn)   { name, (void *)fn, SYM_FUNC }
#define BOTH(name, fn)         HANDLER_AS("Left" name, fn), HANDLER_AS("Right" name, fn)

static const MenuSymbol k_handlers[] = {
    HANDLER(selectUniformMenuOnInit),
    HANDLER(selectUniformMenuOnDraw),
    HANDLER(selectUniformMenuOnReturn),
    HANDLER(selectUniformMenu_OkOnSelect),
    BOTH("ShirtType0_onSelect", type0),
    BOTH("ShirtType1_onSelect", type1),
    BOTH("ShirtType2_onSelect", type2),
    BOTH("ShirtType3_onSelect", type3),
    BOTH("StripesColor_Dec_onSelect", stripesDec),
    BOTH("StripesColor_Inc_onSelect", stripesInc),
    BOTH("BasicColor_Dec_onSelect", basicDec),
    BOTH("BasicColor_Inc_onSelect", basicInc),
    BOTH("ShortsColor_Dec_onSelect", shortsDec),
    BOTH("ShortsColor_Inc_onSelect", shortsInc),
    BOTH("SocksColor_Dec_onSelect", socksDec),
    BOTH("SocksColor_Inc_onSelect", socksInc),
    BOTH("Shirts_onSelect", on_shirt),
    BOTH("Shorts_onSelect", on_shorts),
    BOTH("Socks_onSelect", on_socks),
    BOTH("Switch_onSelect", on_switch),
    BOTH("Reset_onSelect", on_reset),
};

#undef BOTH
#undef HANDLER_AS
#undef HANDLER

void UniformMenu_Init(void)
{
    static int32_t registered;

    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

void UniformMenu_Open(TeamFileEx *left, TeamFileEx *right, int32_t leftIsTop)
{
    uint8_t *menu;
    int32_t i;

    UniformMenu_Init();
    menu = MenuJson_Load("swos2020/selectUniformMenu");
    if (!menu) {
        fprintf(stderr, "[uniform] the screen's menu did not load\n");
        return;
    }
    memset(&s_menu, 0, sizeof s_menu);
    s_menu.team[kLeft] = left;
    s_menu.team[kRight] = right;
    s_menu.editable[kLeft] = s_menu.editable[kRight] = 1;
    s_menu.leftIsTop = leftIsTop;
    if (s_online >= 0) {                    /* the lobby's team screen: the other side is not ours */
        s_menu.editable[kRight] = 0;
        s_menu.leftIsTop = s_onlineTop;
        if (!s_online)
            s_menu.team[kRight] = NULL;
    }
    /* A choice left over for a team that is not in this match is nobody's. */
    for (i = 0; i < kSides; i++)
        if (s_choice[i].chosen && s_choice[i].team != &left->team && (!right || s_choice[i].team != &right->team))
            s_choice[i].chosen = 0;

    ShowMenu(menu);
    KitArt_Return();        /* around ShowMenu: every way out gives the palette back */
}
