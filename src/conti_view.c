/*
 * conti_view.c -- the continental cups' views of a world career (SDL builds; include/conti_view.h,
 * docs/WORLD_2026_27.md 6m): the SWOS 96/97 Mod 4.1's CONTINENTAL CUPS (its tools/ctview.py and
 * ctview.c, MIT, Davide Lorigliola) and VIEW WORLD's green cup buttons.
 *
 *   CONTINENTAL CUPS (VIEW COMPETITIONS entry 8): the confederations whose club cups the world plays
 *   (src/world_conti.c), the career club's first -> a confederation's rounds drawn so far, in the
 *   order they are played (two columns of 8, '<cup> <round>', the cursor on the first not played
 *   yet, after the final on it) -> a round's view; SUMMARY: every cup of the confederation as text
 *   pages (its holder, every round in calendar order, the winner), as the Mod's page.
 *   VIEW WORLD: a continent's screen lists its confederation's cups as green rows (teams.c); one
 *   opens its round being played (before the first: the first drawn; after the final: the final).
 *
 * A round's view: a knockout round's ties with each leg's score (extra time 'ET', penalties 'P'),
 * the club through in yellow; a group stage's tables (played, won, drawn, lost,
 * goals for and against, points: the places going on in light blue) and its results; drawn and not
 * yet played: its ties / groups TO BE PLAYED. PREV / NEXT ROUND walk the cup's rounds.
 *
 * What is shown is the Mod's: the edition in progress replayed from the save (ct_resume, with the
 * goals kept: CT_GREC) -- a round once its month has come in the career's calendar (its draw), its
 * results once that month is over, never past the career club's next game. The replay changes
 * nothing: CT_IN, CT_OUT, CT_SEG and the main rounds' lists are put back as they were. The career
 * club's own games (played in the game's contests) show who went through, not their scores.
 * Without an edition (CT_ED, a career before its cups were built) nothing is in progress.
 */
#include "conti_view.h"

#if defined(SWOS_SDL_BUILD) && SWOS_WORLDS

#include "world_conti.h"
#include "world.h"
#include "team_db.h"
#include "cup_record.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "game_types.h"
#include "comp_identity.h"
#include "diy_competition.h"
#include "text_render.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern uint16_t _selTeamNumber, _isNationalSelector;
extern void *_cupRoundStringTable[];
extern void SetExitMenuFlag(void);
extern void *_charTables[];
extern void DrawMultipleItems(uint16_t maxItems, MenuEntry *firstEntry);

/* ---- the edition, replayed ---- */

static struct ct_out s_out;
static struct ct_seg s_seg;
static u16 s_ids[CT_NC];
static int32_t s_live;                  /* an edition in progress (CT_ED) */
static u8 s_week;
enum { GREC_ROOM = 12288 };
static union { struct ct_grec g; uint8_t b[sizeof(struct ct_grec) + GREC_ROOM]; } s_grec;

static void snapshot(void)
{
    static struct ct_in in0;
    static struct ct_out out0;
    static struct ct_seg seg0;
    static u16 lp0[CT_NCUP][32];
    s_live = 0;
    memset(&s_out, 0, sizeof s_out);
    s_seg.kind = 0xFF;
    s_week = Conti_WeekNow();
    const u8 *spec = World_ContiSpec();
    if (!spec)
        return;
    CT_SPEC = spec;
    if (CT_ED.mark != 0x4543)
        return;
    in0 = CT_IN;
    out0 = CT_OUT;
    seg0 = CT_SEG;
    for (int32_t c = 0; c < CT_NCUP; c++)
        if (CT_LP[c])
            memcpy(lp0[c], CT_LP[c], sizeof lp0[c]);
    memset(&s_grec, 0, sizeof s_grec);
    s_grec.g.room = GREC_ROOM;
    CT_GREC = &s_grec.g;
    ct_resume();
    CT_GREC = 0;
    s_out = CT_OUT;
    s_seg = CT_SEG;
    for (int32_t i = 0; i < CT_NC; i++)
        s_ids[i] = i < CT_IN.nclub ? CT_IN.club[i].id : 0;
    CT_IN = in0;
    CT_OUT = out0;
    CT_SEG = seg0;
    for (int32_t c = 0; c < CT_NCUP; c++)
        if (CT_LP[c])
            memcpy(CT_LP[c], lp0[c], sizeof lp0[c]);
    s_live = 1;
}

/* ---- the spec ---- */

static const WorldContiCup *cups(int32_t *n)
{
    const WorldContiCup *t = World_ContiCups(n);
    const u8 *spec = World_ContiSpec();
    if (!t || !spec) {
        *n = 0;
        return 0;
    }
    if (*n > spec[0])
        *n = spec[0];
    if (*n > CT_NCUP)
        *n = CT_NCUP;
    return t;
}

static const u8 *R(int32_t c, int32_t r) { return ct_round_spec(c, r); }

static int32_t rounds_of(int32_t c)
{
    int32_t r = 0;
    while (r < CT_NRD && R(c, r))
        r++;
    return r;
}

static const char *round_name(int32_t c, int32_t r)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    if (!t || c >= n || r >= 8)
        return "";
    const u8 *rs = R(c, r);
    if (rs && rs[0] == K_GROUPS)                /* (the importer names a group stage by its place: FOURTH ROUND) */
        return rs[19] ? "LEAGUE STAGE" : "GROUP STAGE";
    const int32_t k = t[c].round[r];
    return k >= 0 && k < 23 && _cupRoundStringTable[k] ? (const char *)_cupRoundStringTable[k] : "";
}

/* calendar order: August first (the Mod's run()) */
static int32_t cal_slot(int32_t month) { return (month + 4) % 12; }
static u32 mweek(int32_t m) { return (u32)((m - 1 + (m < 8 ? 12 : 0)) * 8); }

static const char *club(int32_t x)    /* a run's club index + 1 */
{
    static char buf[4][20];
    static int32_t k;
    if (!x || x > CT_NC || !s_ids[x - 1])
        return "-";
    const u16 w = s_ids[x - 1];             /* (the Mod's word: file | ordinal << 8) */
    const TeamDbEntry *e = TeamDb_Team(w & 0xFF, w >> 8);
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "%.17s", e ? (const char *)e->team.team.teamName : "?");
    return b;
}

static const char *word_name(u16 w)
{
    static char b[20];
    const TeamDbEntry *e = TeamDb_Team(w & 0xFF, w >> 8);
    snprintf(b, sizeof b, "%.17s", e ? (const char *)e->team.team.teamName : "?");
    return b;
}

/* round (c, r) runs before the career club's next game (the Mod's seg_before) */
static int32_t seg_before(int32_t c, int32_t r)
{
    if (s_seg.kind == 0xFF)
        return 1;
    const int32_t mo = cal_slot(R(c, r)[9]), smo = cal_slot(R(s_seg.cup, s_seg.round)[9]);
    if (mo != smo)
        return mo < smo;
    if (c != s_seg.cup)
        return c < s_seg.cup;
    return r < s_seg.round;
}

/* 0 not drawn yet, 1 drawn, 2 played (drawn, its month over): the Mod's rstate */
static int32_t rstate(int32_t c, int32_t r)
{
    const u8 *rs = R(c, r);
    const int32_t n = s_out.nent[c][r];
    const int32_t drawn = n && seg_before(c, r) ? 2 : (s_seg.kind != 0xFF && s_seg.cup == c && s_seg.round == r);
    if (s_week < mweek(rs[9]) || !drawn)
        return 0;
    return drawn == 2 && s_week >= mweek(rs[9]) + 8 ? 2 : 1;
}

/* a round with ties or groups to show: drawn, with entrants, not all through without playing */
static int32_t showable(int32_t c, int32_t r)
{
    const u8 *rs = R(c, r);
    const int32_t n = s_out.nent[c][r];
    if (!s_live || !n || !rstate(c, r))
        return 0;
    return !(rs[0] == K_KO && rs[18] && 2 * n <= rs[18]);
}

/* ---- the confederations ---- */

static const char *const k_confName[6] = { "CONMEBOL", "CONCACAF", "AFC", "CAF", "OFC", "FIFA" };
static const uint8_t k_confContinent[6] = { CN_SOUTH_AMERICA, CN_NORTH_AMERICA, CN_ASIA, CN_AFRICA, CN_OCEANIA, 0 };

static int32_t conf_of_team(uint16_t team)          /* our {country, ordinal}: its continent's confederation */
{
    const uint8_t country = (uint8_t)(team >> 8);
    for (int32_t f = 0; f < 5; f++) {
        const uint8_t *in = World_ContinentCountries(k_confContinent[f]);
        for (; in && *in != 255; in++)
            if (*in == country)
                return f;
    }
    return -1;
}

static uint8_t s_confs[6];
static int32_t s_nconfs;

static void list_confs(void)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    const int32_t mine = _isNationalSelector ? -1 : conf_of_team(_selTeamNumber);
    s_nconfs = 0;
    for (int32_t pass = 0; pass < 2; pass++)
        for (int32_t c = 0; c < n; c++) {
            const int32_t f = t[c].conf, seen = 0;
            if ((pass == 0) != (f == mine) || f >= 6)
                continue;
            int32_t dup = seen;
            for (int32_t i = 0; i < s_nconfs; i++)
                if (s_confs[i] == f)
                    dup = 1;
            if (!dup)
                s_confs[s_nconfs++] = (uint8_t)f;
        }
}

/* ---- the screens ---- */

enum { CF_header, CF_note, CF_conf0, CF_exit = CF_conf0 + 6 };
enum { RS_header, RS_note, RS_round0, RS_summary = RS_round0 + 16, RS_exit };
enum { RV_header, RV_subTitle, RV_prev, RV_exit, RV_next, RV_scrollUp, RV_scrollDown, RV_rows };
enum { kRows = 13, kCells = 11, kRoundsMax = 16 };
/* (after the rows: the classic tables' two cell templates and the entry that draws them) */
enum { RV_tHead = RV_rows + kRows * kCells, RV_tText, RV_classic };

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void nav(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

static int32_t s_conf;                  /* the confederation shown */
static uint8_t s_round[kRoundsMax];     /* its rounds' buttons: cup << 4 | round */
static int32_t s_nround;
static char s_roundText[kRoundsMax][48], s_header[48], s_note[64];

/* A label's width in the menus' small font (the rounds' buttons, text flags 0). */
static int32_t small_width(const char *s)
{
    return s && *s ? GetTextWidth((uint8_t *)s, (uint8_t *)_charTables[0]) : 0;
}

/* ---- CONTINENTAL CUPS: the confederations ---- */

static void contiConfsMenu_OnInit(void)
{
    snapshot();
    list_confs();
    for (int32_t i = 0; i < 6; i++) {
        MenuEntry *e = entry_at(CF_conf0 + i);
        e->isInvisible = i >= s_nconfs;
        e->fg = (uintptr_t)(i < s_nconfs ? k_confName[s_confs[i]] : "");
        nav(CF_conf0 + i, -1, -1, i ? CF_conf0 + i - 1 : -1, i + 1 < s_nconfs ? CF_conf0 + i + 1 : CF_exit);
    }
    nav(CF_exit, -1, -1, s_nconfs ? CF_conf0 + s_nconfs - 1 : -1, -1);
    entry_at(CF_note)->fg = (uintptr_t)(s_live ? "" : "NO CUP IN PROGRESS");
}
static void contiConfsMenu_OnDraw(void) {}

static void show_rounds(int32_t conf);
static void conf_pick(int32_t i) { if (i < s_nconfs) show_rounds(s_confs[i]); }
static void contiConfsMenu_conf0(void) { conf_pick(0); }
static void contiConfsMenu_conf1(void) { conf_pick(1); }
static void contiConfsMenu_conf2(void) { conf_pick(2); }
static void contiConfsMenu_conf3(void) { conf_pick(3); }
static void contiConfsMenu_conf4(void) { conf_pick(4); }
static void contiConfsMenu_conf5(void) { conf_pick(5); }
static void contiConfsMenu_exit(void) { SetExitMenuFlag(); }

/* ---- a confederation's rounds ---- */

static int32_t s_euro;                  /* the rounds menu and view show the European cup's record */
static uint8_t s_estage[8];             /* its phases: 0xFF the league phase, else a knockout stage */
static int32_t s_nestage;
static int32_t s_ek;                    /* the phase shown (-1: its summary) */
static char s_euroName[48];
static int32_t s_autoOpen;              /* CONTINENTAL CUPS: open the round in progress once */
static int32_t s_sel;                   /* the rounds' cursor (the Mod's CR_SEL) */
static int32_t s_picked = -1;           /* the round opened from it */
static int32_t s_only = -1;             /* (VIEW WORLD: one cup) */

static void list_rounds(void)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    uint8_t all[CT_NCUP * CT_NRD], pl[CT_NCUP * CT_NRD];
    int32_t k = 0;
    for (int32_t c = 0; c < n; c++) {
        if (t[c].conf != s_conf || (s_only >= 0 && c != s_only))
            continue;
        const int32_t nr = rounds_of(c);
        for (int32_t mo = 0; mo < 12; mo++)
            for (int32_t r = 0; r < nr; r++) {
                if (cal_slot(R(c, r)[9]) != mo || !showable(c, r))
                    continue;
                pl[k] = rstate(c, r) == 2;
                all[k++] = (uint8_t)(c << 4 | r);
            }
    }
    if (getenv("SWOS_CONTI_VIEW_DEBUG")) {
        printf("[conti-view] conf %d: week %d, live %d, seg kind %02X cup %d round %d\n", (int)s_conf, (int)s_week,
               (int)s_live, (unsigned)s_seg.kind, (int)s_seg.cup, (int)s_seg.round);
        for (int32_t c = 0; c < n; c++)
            for (int32_t r = 0; r < rounds_of(c); r++)
                printf("[conti-view]   cup %d round %d month %d: %d entrants, state %d\n", (int)c, (int)r,
                       (int)R(c, r)[9], (int)s_out.nent[c][r], (int)rstate(c, r));
        fflush(stdout);
    }
    const int32_t s = k > kRoundsMax ? k - kRoundsMax : 0;
    s_nround = k - s;
    for (int32_t i = s; i < k; i++)
        s_round[i - s] = all[i];
    s_sel = s_nround ? s_nround - 1 : 0;
    for (int32_t i = k; i-- > s; )
        if (!pl[i])
            s_sel = i - s;
}

/* The rounds' buttons (Playa, 2026-11-16: centred, each label inside its button): up to 8 one
 * column, centred under the header and as wide as the longest label
 * needs; 9 to 16 two columns as wide as the screen gives. A label still too wide for its column
 * has its round's name shortened (round_short). */
enum { RB_PAD = 14, RB_MIN = 150, RB_MAX = 296, RB_COL = 154 };

static const char *round_short(const char *r)
{
    static const struct { const char *full, *brief; } k[] = {
        { "PRELIMINARY ROUND", "PRELIM. ROUND" }, { "FIRST ROUND", "1ST ROUND" }, { "SECOND ROUND", "2ND ROUND" },
        { "THIRD ROUND", "3RD ROUND" }, { "QUARTER-FINALS", "QUARTER-F." }, { "QUARTER-FINAL", "QUARTER-F." },
        { "SEMI-FINALS", "SEMI-F." }, { "SEMI-FINAL", "SEMI-F." }, { "GROUP STAGE", "GROUPS" },
        { "LEAGUE STAGE", "LEAGUE" }, { "ROUND OF 16", "LAST 16" },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strcmp(r, k[i].full))
            return k[i].brief;
    return r;
}

static void layout_rounds(int32_t n, const char *const *cup, const char *const *round)
{
    const int32_t two = n > 8;
    int32_t widest = 0;
    for (int32_t k = 0; k < n; k++) {
        char *t = s_roundText[k];
        snprintf(t, sizeof s_roundText[k], "%s%s%s", cup[k], cup[k][0] ? " " : "", round[k]);
        if (two && small_width(t) + RB_PAD > RB_COL)
            snprintf(t, sizeof s_roundText[k], "%s%s%s", cup[k], cup[k][0] ? " " : "", round_short(round[k]));
        if (two && small_width(t) + RB_PAD > RB_COL) {      /* (still too wide: without " ROUND") */
            char *w = strstr(t, " ROUND");
            if (w && !w[6])
                *w = '\0';
        }
        entry_at(RS_round0 + k)->fg = (uintptr_t)t;
        if (small_width(t) > widest)
            widest = small_width(t);
    }
    int32_t w = widest + RB_PAD;
    w = w < RB_MIN ? RB_MIN : w > RB_MAX ? RB_MAX : w;
    /* (centred on the header as it is drawn: the menu's entries carry the screen's offset) */
    const MenuEntry *head = entry_at(RS_header);
    const int32_t mid = head->x + head->dwWidth / 2;
    if (getenv("SWOS_CONTI_VIEW_DEBUG")) {
        printf("[conti-view] layout: header x %d w %d, summary x %d, exit x %d, widest %d\n", (int)head->x,
               (int)head->dwWidth, (int)entry_at(RS_summary)->x, (int)entry_at(RS_exit)->x, (int)widest);
        fflush(stdout);
    }
    for (int32_t k = 0; k < kRoundsMax; k++) {
        MenuEntry *e = entry_at(RS_round0 + k);
        if (two) {
            e->x = (int16_t)(k < 8 ? mid - 2 - RB_COL : mid + 2);
            e->dwWidth = RB_COL;
        } else {
            e->x = (int16_t)(mid - w / 2);
            e->dwWidth = (uint16_t)w;
        }
    }
}

static void euro_stages(void);
static const char *euro_stage_name(int32_t k);
static void euroRoundsInit(void)
{
    euro_stages();
    snprintf(s_header, sizeof s_header, "%s", s_euroName);
    entry_at(RS_header)->fg = (uintptr_t)s_header;
    entry_at(RS_note)->fg = (uintptr_t)(s_nestage ? "" : "NOTHING PLAYED YET");
    s_nround = s_nestage;
    const char *cup[kRoundsMax], *round[kRoundsMax];
    for (int32_t k = 0; k < kRoundsMax; k++) {
        MenuEntry *e = entry_at(RS_round0 + k);
        e->isInvisible = k >= s_nestage;
        e->bgColor = 0x0E;                  /* (the Mod's green boxes) */
        if (k < s_nestage) {
            cup[k] = "";
            round[k] = euro_stage_name(k);
        }
    }
    layout_rounds(s_nestage, cup, round);
    const int32_t n0 = s_nround < 8 ? s_nround : 8;
    for (int32_t k = 0; k < s_nround; k++)
        nav(RS_round0 + k, -1, -1, k ? RS_round0 + k - 1 : -1, k + 1 < s_nround ? RS_round0 + k + 1 : RS_summary);
    nav(RS_summary, -1, RS_exit, n0 ? RS_round0 + n0 - 1 : -1, -1);
    nav(RS_exit, RS_summary, -1, n0 ? RS_round0 + n0 - 1 : -1, -1);
    s_sel = s_picked >= 0 && s_picked < s_nround ? s_picked : s_nround ? s_nround - 1 : 0;
    SetCurrentEntry((uint16_t)(s_nround ? RS_round0 + s_sel : RS_summary));
}

static void contiRoundsMenu_OnInit(void)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    if (s_euro) {
        euroRoundsInit();
        return;
    }
    list_rounds();
    snprintf(s_header, sizeof s_header, "%s", k_confName[s_conf]);
    entry_at(RS_header)->fg = (uintptr_t)s_header;
    entry_at(RS_note)->fg = (uintptr_t)(s_nround ? "" : s_live ? "NO ROUND DRAWN YET" : "NO CUP IN PROGRESS");
    const char *cup[kRoundsMax], *round[kRoundsMax];
    for (int32_t k = 0; k < kRoundsMax; k++) {
        MenuEntry *e = entry_at(RS_round0 + k);
        e->isInvisible = k >= s_nround;
        e->bgColor = 0x0D;
        if (k < s_nround) {
            const int32_t c = s_round[k] >> 4, r = s_round[k] & 15;
            cup[k] = t[c].shortName;
            round[k] = round_name(c, r);
        }
    }
    layout_rounds(s_nround, cup, round);
    /* two columns of 8: the first column's rows down to SUMMARY, the second's to EXIT */
    const int32_t n0 = s_nround < 8 ? s_nround : 8, n1 = s_nround - n0;
    for (int32_t k = 0; k < s_nround; k++) {
        const int32_t col = k / 8, row = k % 8, e = RS_round0 + k;
        const int32_t last = col ? row == n1 - 1 : row == n0 - 1;
        nav(e, col && row < n0 ? e - 8 : -1, !col && row < n1 ? e + 8 : -1, row ? e - 1 : -1,
            last ? (col ? RS_exit : RS_summary) : e + 1);
    }
    nav(RS_summary, -1, RS_exit, n0 ? RS_round0 + n0 - 1 : -1, -1);
    nav(RS_exit, RS_summary, -1, n1 ? RS_round0 + 8 + n1 - 1 : n0 ? RS_round0 + n0 - 1 : -1, -1);
    if (s_picked >= 0 && s_picked < s_nround)   /* (back from a round: on it, as the Mod's) */
        s_sel = s_picked;
    SetCurrentEntry((uint16_t)(s_nround ? RS_round0 + s_sel : RS_exit));
}
static void round_pick(int32_t k);
static void contiRoundsMenu_OnDraw(void)
{
    /* CONTINENTAL CUPS opens straight on the round in progress (drawn, not played: the Mod's CR_IN);
     * before a season's first, between rounds and after the final: the list */
    if (s_autoOpen && !s_euro) {
        s_autoOpen = 0;
        for (int32_t k = 0; k < s_nround; k++)
            if (rstate(s_round[k] >> 4, s_round[k] & 15) == 1) {
                round_pick(k);
                return;
            }
    }
}

static void show_round(int32_t c, int32_t r);
static void show_summary(void);
static void show_stage(int32_t k);
static void round_pick(int32_t k)
{
    if (k < s_nround) {
        s_sel = s_picked = k;
        if (s_euro)
            show_stage(k);
        else
            show_round(s_round[k] >> 4, s_round[k] & 15);
    }
}
static void contiRoundsMenu_round0(void) { round_pick(0); }
static void contiRoundsMenu_round1(void) { round_pick(1); }
static void contiRoundsMenu_round2(void) { round_pick(2); }
static void contiRoundsMenu_round3(void) { round_pick(3); }
static void contiRoundsMenu_round4(void) { round_pick(4); }
static void contiRoundsMenu_round5(void) { round_pick(5); }
static void contiRoundsMenu_round6(void) { round_pick(6); }
static void contiRoundsMenu_round7(void) { round_pick(7); }
static void contiRoundsMenu_round8(void) { round_pick(8); }
static void contiRoundsMenu_round9(void) { round_pick(9); }
static void contiRoundsMenu_round10(void) { round_pick(10); }
static void contiRoundsMenu_round11(void) { round_pick(11); }
static void contiRoundsMenu_round12(void) { round_pick(12); }
static void contiRoundsMenu_round13(void) { round_pick(13); }
static void contiRoundsMenu_round14(void) { round_pick(14); }
static void contiRoundsMenu_round15(void) { round_pick(15); }
static void contiRoundsMenu_summary(void) { show_summary(); }
static void contiRoundsMenu_exit(void) { SetExitMenuFlag(); }

/* ---- a round's view: lines, 13 a page ---- */

enum { L_TEXT, L_HEAD, L_TIE, L_TABLE, L_TABHEAD };
typedef struct {
    uint8_t kind, win;                  /* L_TIE: 1 the first club through, 2 the second; L_TABLE: a qualifier */
    char a[64], b[20], c[24];
    char k[7][5];
} Line;
enum { kLinesMax = 400 };
static Line s_lines[kLinesMax];
static int32_t s_nlines, s_top;

/* A group stage's tables the classic way (Playa, 2026-11-16): SWOS's own tournament groups
 * (diyLeagueTournamentMenu, DIYLeagueTournament16BeforeDraw) -- an orange head row (the group, PL W D
 * L F A PTS), a row a club: its place, its name (a computer club red, grey once through, the
 * career club blue), its figures on grey -- each group's results right under its table, then the
 * next group (a page as many as fit; results that do not, on the next page). */
enum { GT_MAX = 16, GT_ROWS = 16, GT_CHUNKS = 64 };
typedef struct { char place[4], club[20], k[7][5]; uint8_t colour; } GtRow;
typedef struct {
    char name[48];
    uint8_t nrows;
    int16_t line0, nline;               /* its results: s_lines[line0 ..] */
    GtRow row[GT_ROWS];
} GroupTab;
static GroupTab s_gt[GT_MAX];
static int32_t s_ngt;
static struct { uint8_t g, first, n, page; int16_t y; } s_chunk[GT_CHUNKS];
static int32_t s_nchunk, s_tpages, s_tpage;     /* a group stage's pages (0: not one), the one shown */
static uint8_t s_lpage[kLinesMax];              /* its result lines: the page each is on, and where */
static int16_t s_ly[kLinesMax];
enum { CG_TOP = 19, CG_BOTTOM = 180, CG_HEAD = 8, CG_ROW = 6, CG_STEP = 7, CG_GAP = 5, CG_LINE = 8 };
static int32_t s_vc, s_vr;              /* the round shown (-1: the summary) */
static char s_viewHeader[72], s_viewSub[64];

static Line *add(int32_t kind)
{
    if (s_nlines >= kLinesMax)
        return &s_lines[kLinesMax - 1];
    Line *l = &s_lines[s_nlines++];
    memset(l, 0, sizeof *l);
    l->kind = (uint8_t)kind;
    return l;
}
static void text_line(int32_t kind, const char *s) { snprintf(add(kind)->a, sizeof s_lines[0].a, "%s", s); }
static void keep_together(int32_t k)    /* the next k lines on one page */
{
    const int32_t at = s_nlines % kRows;
    if (at && at + k > kRows)
        while (s_nlines % kRows && s_nlines < kLinesMax)
            add(L_TEXT);
}

/* a goal byte of the replay: home + 1 | (away + 1) << 4; 0 none */
static int32_t gh(u8 v) { return (v & 15) - 1; }
static int32_t ga(u8 v) { return (v >> 4) - 1; }

/* The game's own matches of a conti round (cup_record.c: the career club's ties and group, which the
 * engine's replay marks CT_GR_OWN): club index x -> our team number; the matches of round (c, r)
 * between two clubs (either way round), in the order played. */
static uint16_t our_team(int32_t x)
{
    if (!x || x > CT_NC || !s_ids[x - 1])
        return 0xFFFF;
    const u16 w = s_ids[x - 1];
    return (uint16_t)((w & 0xFF) << 8 | w >> 8);
}

static int32_t own_games(int32_t c, int32_t r, int32_t x, int32_t y, const CupRecGame **out, int32_t max)
{
    const CupRecordState *rec = CupRecord_Conti();
    const uint16_t a = our_team(x), b = our_team(y);
    int32_t k = 0;
    for (int32_t i = 0; rec && i < rec->nConti && k < max; i++) {
        const CupRecGame *g = &rec->conti[i];
        /* (a group's: its round; a tie's: the cup's knockout matches of the two -- the game contest's
         * later rounds are recorded under the round its stage began in, CT_SEG moving after them) */
        const int32_t round_ok = R(c, r)[0] == K_GROUPS ? g->round == (uint8_t)(c << 4 | r)
                                                       : (g->round >> 4) == c && !(g->flags & CUPREC_LEAGUE);
        if (round_ok && ((g->t1 == a && g->t2 == b) || (g->t1 == b && g->t2 == a)))
            out[k++] = g;
    }
    return k;
}

static uint16_t our_mgr(void)
{
    return CT_ED.mgr ? (uint16_t)((CT_ED.mgr & 0xFF) << 8 | CT_ED.mgr >> 8) : 0xFFFF;
}

/* the career club's knockout matches of round (c, r) the game played */
static int32_t own_tie(int32_t c, int32_t r, const CupRecGame **out, int32_t max)
{
    const CupRecordState *rec = CupRecord_Conti();
    const uint16_t me = our_mgr();
    int32_t k = 0;
    for (int32_t i = 0; rec && i < rec->nConti && k < max; i++) {
        const CupRecGame *g = &rec->conti[i];
        if (g->round == (uint8_t)(c << 4 | r) && !(g->flags & CUPREC_LEAGUE) && (g->t1 == me || g->t2 == me))
            out[k++] = g;
    }
    return k;
}

/* a tie's score from the game's matches: club a's goals first; ET / P */
static void own_tie_score_t(Line *l, uint16_t a, const CupRecGame *const *g, int32_t n)
{
    int32_t ta = 0, tb = 0, et = 0, pens = 0;
    l->b[0] = 0;
    for (int32_t i = 0; i < n && i < 2; i++) {
        const int32_t ga1 = g[i]->t1 == a ? g[i]->g1 : g[i]->g2, gb1 = g[i]->t1 == a ? g[i]->g2 : g[i]->g1;
        snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), "%s%d-%d", i ? " " : "", (int)ga1, (int)gb1);
        ta += ga1;
        tb += gb1;
        et |= (g[i]->flags & CUPREC_ET) != 0;
        pens |= (g[i]->flags & CUPREC_PENS) != 0;
    }
    if (pens)
        snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), " P");
    else if (et)
        snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), " ET");
}

static void ko_lines(int32_t c, int32_t r, int32_t played)
{
    const u8 *rs = R(c, r);
    const int32_t n = s_out.nent[c][r];
    const u16 off = s_grec.g.off[c][r];
    for (int32_t i = 0; i + 1 < n; i += 2) {
        Line *l = add(L_TIE);
        snprintf(l->a, sizeof l->a, "%s", club(s_out.ent[c][r][i]));
        snprintf(l->c, sizeof l->c, "%s", club(s_out.ent[c][r][i + 1]));
        if (!played) {
            snprintf(l->b, sizeof l->b, "-");
            continue;
        }
        l->win = s_out.res[c][r][i] ? 1 : 2;
        const u8 *g = off ? s_grec.g.b + off - 1 + 3 * (i / 2) : 0;
        if (!g || g[0] == CT_GR_OWN || !g[0]) {
            /* (the career club's: the game's tie -- its opponent the game contest's draw, which may not be
             * the engine's; its scores) */
            const CupRecGame *mine[2];
            const int32_t me = s_out.ent[c][r][i] && our_team(s_out.ent[c][r][i]) == our_mgr() ? 0 : 1;
            const int32_t k = own_tie(c, r, mine, 2);
            if (k) {
                const uint16_t opp = mine[0]->t1 == our_mgr() ? mine[0]->t2 : mine[0]->t1;
                char *other = me ? l->a : l->c;
                snprintf(other, me ? sizeof l->a : sizeof l->c, "%s", word_name((u16)(opp >> 8 | (opp & 0xFF) << 8)));
                own_tie_score_t(l, me ? opp : our_mgr(), mine, k);
            } else
                snprintf(l->b, sizeof l->b, "PLAYED");
            continue;
        }
        int32_t a = gh(g[0]), b = ga(g[0]);
        if (rs[1] == 2 && g[1]) {
            const int32_t a2 = ga(g[1]), b2 = gh(g[1]);    /* (the second leg: the second club at home) */
            snprintf(l->b, sizeof l->b, "%d-%d %d-%d", (int)a, (int)b, (int)a2, (int)b2);
            a += a2;
            b += b2;
        } else
            snprintf(l->b, sizeof l->b, "%d-%d", (int)a, (int)b);
        if (g[2]) {
            const int32_t ea = gh(g[2]), eb = ga(g[2]);
            snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), ea != eb ? " ET" : " P");
        } else if (a == b && rs[1] != 2)
            snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), " P");
        else if (a == b && rs[1] == 2 && (rs[2] == 0 || rs[2] == 1))
            snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), " P");
    }
}

static void put_num(Line *l, int32_t j, int32_t v) { snprintf(l->k[j], sizeof l->k[j], "%d", (int)v); }

static void group_lines(int32_t c, int32_t r, int32_t played)
{
    const u8 *rs = R(c, r);
    const int32_t ng = rs[6], gs = rs[7], grounds = rs[8] ? rs[8] : 1, n = s_out.nent[c][r];
    const int32_t half = gs * (gs - 1) / 2, gm = half * grounds;
    const u16 off = s_grec.g.off[c][r];
    for (int32_t g = 0; g < ng; g++) {
        int32_t clubs[16], pts[16] = {0}, pl[16] = {0}, w[16] = {0}, d[16] = {0}, lo[16] = {0}, gf[16] = {0},
                gag[16] = {0}, place[16];
        int32_t m = 0, own = 0;
        for (int32_t k = 0; k < gs && k < 16; k++) {
            const int32_t i = g * gs + k;
            clubs[k] = i < n ? s_out.ent[c][r][i] : 0;
            place[k] = played && i < n ? s_out.res[c][r][i] : k + 1;
        }
        const u8 *gb = off ? s_grec.g.b + off - 1 + g * gm : 0;
        const CupRecGame *mine[160];        /* (the career club's group: the game's matches) */
        int32_t nmine = 0;
        if (played && gb && gb[0] == CT_GR_OWN) {
            for (int32_t i = 0; i < gs; i++)
                for (int32_t j = i + 1; j < gs; j++) {
                    const CupRecGame *two[4];
                    const int32_t k = own_games(c, r, clubs[i], clubs[j], two, 4);
                    for (int32_t q = 0; q < k && nmine < 160; q++)
                        mine[nmine++] = two[q];
                }
            for (int32_t q = 0; q < nmine; q++) {
                int32_t h = -1, a = -1;
                for (int32_t k = 0; k < gs; k++) {
                    if (our_team(clubs[k]) == mine[q]->t1) h = k;
                    if (our_team(clubs[k]) == mine[q]->t2) a = k;
                }
                const int32_t x = mine[q]->g1, y = mine[q]->g2;
                pl[h]++; pl[a]++; gf[h] += x; gag[h] += y; gf[a] += y; gag[a] += x;
                if (x > y) { w[h]++; lo[a]++; pts[h] += 3; }
                else if (x < y) { w[a]++; lo[h]++; pts[a] += 3; }
                else { d[h]++; d[a]++; pts[h]++; pts[a]++; }
            }
        }
        if (played && gb && !nmine)
            for (int32_t leg = 0; leg < grounds; leg++)
                for (int32_t i = 0; i < gs; i++)
                    for (int32_t j = i + 1; j < gs; j++, m++) {
                        const u8 v = gb[m];
                        if (v == CT_GR_OWN) { own = 1; continue; }
                        if (!v) continue;
                        const int32_t h = leg ? j : i, a = leg ? i : j, x = gh(v), y = ga(v);
                        pl[h]++; pl[a]++; gf[h] += x; gag[h] += y; gf[a] += y; gag[a] += x;
                        if (x > y) { w[h]++; lo[a]++; pts[h] += 3; }
                        else if (x < y) { w[a]++; lo[h]++; pts[a] += 3; }
                        else { d[h]++; d[a]++; pts[h]++; pts[a]++; }
                    }
        char t[48];
        if (ng > 1)
            snprintf(t, sizeof t, "GROUP %c", (char)('A' + g));
        else
            snprintf(t, sizeof t, "%s", round_name(c, r));
        /* the table, the classic way (GroupTab); before a ball is kicked its clubs on nought, as the
         * game's own tournaments show them; the career club's group without the game's matches: the
         * places alone */
        const int32_t figures = !played || (gb && (!own || nmine));
        const int32_t up = rs[19] ? rs[19] : 2;             /* (the places going on: a league stage's top n, else 2) */
        const uint16_t me = our_mgr();
        GroupTab *tab = s_ngt < GT_MAX ? &s_gt[s_ngt++] : &s_gt[GT_MAX - 1];
        memset(tab, 0, sizeof *tab);
        snprintf(tab->name, sizeof tab->name, "%s", t);
        for (int32_t q = 1; q <= gs; q++)
            for (int32_t k = 0; k < gs; k++) {
                if (!clubs[k] || place[k] != q || tab->nrows >= GT_ROWS)
                    continue;
                Line row;                                   /* (put_num's cells) */
                memset(&row, 0, sizeof row);
                if (figures) {
                    put_num(&row, 0, pl[k]); put_num(&row, 1, w[k]); put_num(&row, 2, d[k]); put_num(&row, 3, lo[k]);
                    put_num(&row, 4, gf[k]); put_num(&row, 5, gag[k]); put_num(&row, 6, pts[k]);
                }
                GtRow *o = &tab->row[tab->nrows++];
                snprintf(o->place, sizeof o->place, "%d", (int)q);
                snprintf(o->club, sizeof o->club, "%s", club(clubs[k]));
                memcpy(o->k, row.k, sizeof o->k);
                o->colour = me != 0xFFFF && our_team(clubs[k]) == me ? 11 : played && q <= up ? 7 : 10;
            }
        /* its results, right under its table */
        tab->line0 = (int16_t)s_nlines;
        if (nmine)                                          /* (the results: the game's) */
            for (int32_t q = 0; q < nmine; q++) {
                Line *l = add(L_TIE);
                snprintf(l->a, sizeof l->a, "%s", word_name((u16)(mine[q]->t1 >> 8 | (mine[q]->t1 & 0xFF) << 8)));
                snprintf(l->c, sizeof l->c, "%s", word_name((u16)(mine[q]->t2 >> 8 | (mine[q]->t2 & 0xFF) << 8)));
                snprintf(l->b, sizeof l->b, "%d-%d", (int)mine[q]->g1, (int)mine[q]->g2);
                l->win = mine[q]->g1 > mine[q]->g2 ? 1 : mine[q]->g1 < mine[q]->g2 ? 2 : 0;
            }
        else if (played && !own && gb) {                    /* (the results) */
            m = 0;
            for (int32_t leg = 0; leg < grounds; leg++)
                for (int32_t i = 0; i < gs; i++)
                    for (int32_t j = i + 1; j < gs; j++, m++) {
                        const u8 v = gb[m];
                        if (!v || v == CT_GR_OWN) continue;
                        const int32_t h = leg ? j : i, a = leg ? i : j;
                        Line *l = add(L_TIE);
                        snprintf(l->a, sizeof l->a, "%s", club(clubs[h]));
                        snprintf(l->c, sizeof l->c, "%s", club(clubs[a]));
                        snprintf(l->b, sizeof l->b, "%d-%d", (int)gh(v), (int)ga(v));
                        l->win = gh(v) > ga(v) ? 1 : gh(v) < ga(v) ? 2 : 0;
                    }
        } else if (played && own && !nmine)
            text_line(L_TEXT, "  (YOUR CLUB'S GROUP: ITS PLACES AS YOU PLAYED IT)");
        tab->nline = (int16_t)(s_nlines - tab->line0);
    }
}

/* A group stage onto pages: a group (its table, its results under it) on the page it fits whole,
 * else from a new page; results that do not fit go on over the next (a table longer than a page
 * goes over several, its head on each). A page's lines are the round view's rows (kRows of them). */
static void layout_classic(void)
{
    const int32_t most = (CG_BOTTOM - CG_TOP - CG_HEAD - 1) / CG_STEP;
    int32_t page = 0, y = CG_TOP, slots = 0;
    s_nchunk = 0;
    for (int32_t g = 0; g < s_ngt; g++) {
        const int32_t rows = s_gt[g].nrows, nl = s_gt[g].nline;
        const int32_t whole = CG_HEAD + 1 + rows * CG_STEP + (nl ? 1 + nl * CG_LINE : 0);
        if (y > CG_TOP && (y + whole > CG_BOTTOM || slots + nl > kRows)) {
            page++;
            y = CG_TOP;
            slots = 0;
        }
        int32_t first = 0;
        do {
            const int32_t room = (CG_BOTTOM - y - CG_HEAD - 1) / CG_STEP, left = rows - first;
            if (y > CG_TOP && (room < 1 || (room < left && left <= most))) {
                page++;
                y = CG_TOP;
                slots = 0;
                continue;
            }
            const int32_t n = left < room ? left : room;
            if (s_nchunk < GT_CHUNKS) {
                s_chunk[s_nchunk].g = (uint8_t)g;
                s_chunk[s_nchunk].first = (uint8_t)first;
                s_chunk[s_nchunk].n = (uint8_t)n;
                s_chunk[s_nchunk].page = (uint8_t)page;
                s_chunk[s_nchunk].y = (int16_t)y;
                s_nchunk++;
            }
            y += CG_HEAD + 1 + n * CG_STEP;
            first += n;
        } while (first < rows);
        y += 1;
        for (int32_t i = 0; i < nl; i++) {
            if (y + CG_LINE > CG_BOTTOM || slots >= kRows) {
                page++;
                y = CG_TOP;
                slots = 0;
            }
            s_lpage[s_gt[g].line0 + i] = (uint8_t)page;
            s_ly[s_gt[g].line0 + i] = (int16_t)y;
            y += CG_LINE;
            slots++;
        }
        y += CG_GAP;
    }
    s_tpages = s_nchunk ? page + 1 : 0;
}

/* (the classic tables' entry: drawn over the round's view while one of their pages is shown) */
static void contiRoundMenu_classic(MenuEntry *self)
{
    (void)self;
    if (s_tpage >= s_tpages)
        return;
    static const int16_t kX[7] = { 126, 151, 176, 201, 226, 251, 276 };
    static const char *const kHead[7] = { "PL", "W", "D", "L", "F", "A", "PTS" };
    /* (the templates sit at the menu's (0, 0): the screen's offset is theirs) */
    const MenuEntry *th = entry_at(RV_tHead), *tt = entry_at(RV_tText);
    MenuEntry cell[9];
#define CELL(i, tmpl, cx, cy, cw, ch, text, colour, flags) do { \
        cell[i] = *(tmpl); cell[i].isInvisible = 0; \
        cell[i].x = (int16_t)((tmpl)->x + (cx)); cell[i].y = (int16_t)((tmpl)->y + (cy)); \
        cell[i].dwWidth = (uint16_t)(cw); cell[i].dwHeight = (uint16_t)(ch); cell[i].fg = (uintptr_t)(text); \
        cell[i].stringFlags = (uint16_t)(flags); \
        if ((colour) >= 0) cell[i].bgColor = (uint16_t)((cell[i].bgColor & 0xFFF0) | (colour)); \
    } while (0)
    for (int32_t q = 0; q < s_nchunk; q++) {
        if (s_chunk[q].page != s_tpage)
            continue;
        const GroupTab *g = &s_gt[s_chunk[q].g];
        int16_t y = s_chunk[q].y;
        CELL(0, th, 25, y, 100, CG_HEAD, g->name, 5, 0);
        for (int32_t j = 0; j < 7; j++)
            CELL(1 + j, th, kX[j], y, 24, CG_HEAD, kHead[j], 5, 0);
        DrawMultipleItems(8, cell);
        y += CG_HEAD + 1;
        for (int32_t i = s_chunk[q].first; i < s_chunk[q].first + s_chunk[q].n; i++, y += CG_STEP) {
            CELL(0, tt, 6, y, 18, CG_ROW, g->row[i].place, -1, 0);
            CELL(1, th, 25, y, 100, CG_ROW, g->row[i].club, g->row[i].colour, 0x8000);
            for (int32_t j = 0; j < 7; j++)
                CELL(2 + j, th, kX[j], y, 24, CG_ROW, g->row[i].k[j], 7, 0);
            DrawMultipleItems(9, cell);
        }
    }
#undef CELL
}

static void build_round(int32_t c, int32_t r)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    s_nlines = 0;
    s_top = 0;
    const int32_t st = rstate(c, r), played = st == 2;
    snprintf(s_viewHeader, sizeof s_viewHeader, "%s", t[c].name);
    snprintf(s_viewSub, sizeof s_viewSub, "%s%s", round_name(c, r), played ? "" : " - TO BE PLAYED");
    if (R(c, r)[0] == K_GROUPS) {
        /* (the classic tables have the subtitle's line: the round goes in the header, as the game's
         *  own "EUROPEAN CHAMPIONS CUP FIRST ROUND") */
        snprintf(s_viewHeader, sizeof s_viewHeader, "%s %s%s", t[c].name, round_name(c, r), played ? "" : " - TO BE PLAYED");
        snprintf(s_viewSub, sizeof s_viewSub, "%s", "");
        group_lines(c, r, played);
        layout_classic();
    } else
        ko_lines(c, r, played);
    while (s_nlines && s_lines[s_nlines - 1].kind == L_TEXT && !s_lines[s_nlines - 1].a[0])
        s_nlines--;
}

/* The SUMMARY: every cup of the confederation, as the Mod's page (ctview.c cup_lines) */
static void summary_lines(void)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    char b[80];
    s_nlines = 0;
    s_top = 0;
    snprintf(s_viewHeader, sizeof s_viewHeader, "%s", k_confName[s_conf]);
    snprintf(s_viewSub, sizeof s_viewSub, "SUMMARY");
    int32_t any = 0;
    for (int32_t c = 0; c < n; c++) {
        if (t[c].conf != s_conf || (s_only >= 0 && c != s_only))
            continue;
        any = 1;
        while (s_nlines % kRows && s_nlines < kLinesMax)   /* (each cup from a new page) */
            add(L_TEXT);
        text_line(L_HEAD, t[c].name);
        if (s_live && CT_ST.mark == 0x5443 && CT_ST.hold[c].id) {
            snprintf(b, sizeof b, "HOLDER: %s", word_name(CT_ST.hold[c].id));
            text_line(L_TEXT, b);
        }
        if (!s_live)
            continue;
        int32_t done = 1;
        const int32_t nr = rounds_of(c);
        for (int32_t mo = 0; mo < 12; mo++)
            for (int32_t r = 0; r < nr; r++) {
                const u8 *rs = R(c, r);
                if (cal_slot(rs[9]) != mo)
                    continue;
                const int32_t ne = s_out.nent[c][r];
                if (rs[0] == K_KO && rs[18] && ne && 2 * ne <= rs[18])
                    continue;
                if (!ne && seg_before(c, r))
                    continue;
                const int32_t st = rstate(c, r);
                if (!st) {
                    snprintf(b, sizeof b, "%s: TO BE PLAYED", round_name(c, r));
                    text_line(L_TEXT, b);
                    done = 0;
                    continue;
                }
                if (st != 2)
                    done = 0;
                if (rs[0] == K_GROUPS) {
                    const int32_t ng = rs[6], gs = rs[7];
                    for (int32_t g = 0; g < ng; g++) {
                        if (ng > 1)
                            snprintf(b, sizeof b, "GROUP %c:%s", (char)('A' + g), st == 2 ? "" : " TO BE PLAYED");
                        else
                            snprintf(b, sizeof b, "%s:%s", round_name(c, r), st == 2 ? "" : " TO BE PLAYED");
                        keep_together(2);
                        text_line(L_TEXT, b);
                        b[0] = 0;
                        for (int32_t q = 1; q <= gs; q++)
                            for (int32_t k = 0; k < gs; k++) {
                                const int32_t i = g * gs + k;
                                if (i >= ne || !s_out.ent[c][r][i])
                                    continue;
                                if (st == 2 ? s_out.res[c][r][i] != q : k + 1 != q)
                                    continue;
                                char one[28];
                                snprintf(one, sizeof one, st == 2 ? "%d %s" : "%.0d%s", (int)q, club(s_out.ent[c][r][i]));
                                if (strlen(b) + strlen(one) + 3 > 52) {
                                    text_line(L_TEXT, b);
                                    b[0] = 0;
                                }
                                snprintf(b + strlen(b), sizeof b - strlen(b), "%s%s", b[0] ? ", " : "  ", one);
                            }
                        if (b[0])
                            text_line(L_TEXT, b);
                    }
                    continue;
                }
                snprintf(b, sizeof b, "%s:%s", round_name(c, r), st == 2 ? "" : " TO BE PLAYED");
                keep_together(2);
                text_line(L_TEXT, b);
                for (int32_t i = 0; i + 1 < ne; i += 2) {
                    const int32_t w = st == 2 ? (s_out.res[c][r][i] ? 1 : 2) : 0;
                    snprintf(b, sizeof b, "  %s%s%s - %s%s%s", w == 1 ? "(" : "", club(s_out.ent[c][r][i]),
                             w == 1 ? ")" : "", w == 2 ? "(" : "", club(s_out.ent[c][r][i + 1]), w == 2 ? ")" : "");
                    text_line(L_TEXT, b);
                }
            }
        if (done && s_out.winner[c]) {
            snprintf(b, sizeof b, "WINNER: %s", club(s_out.winner[c]));
            text_line(L_HEAD, b);
        }
    }
    if (!any || !s_live)
        text_line(L_TEXT, "NO CUP IN PROGRESS");
}

static int32_t view_pages(void) { return s_tpages ? s_tpages : (s_nlines + kRows - 1) / kRows; }

/* A row's cells to (y, h) -- the JSON's own: 32 + 11 a row, 10 tall (y relative to the menu). */
static void place_row(int32_t base, int32_t y, int32_t h)
{
    const int16_t oy = entry_at(RV_tHead)->y;   /* (the menu's (0, 0)) */
    for (int32_t c = 0; c < kCells; c++) {
        MenuEntry *e = entry_at(base + c);
        e->y = (int16_t)(oy + y);
        e->dwHeight = (uint16_t)h;
    }
}

static void refresh_view(void)
{
    const int32_t classic = s_tpages > 0;
    MenuEntry *head = entry_at(RV_header), *sub = entry_at(RV_subTitle);
    head->fg = (uintptr_t)s_viewHeader;
    /* (the game's big letters while the header's text fits them, else its small ones) */
    head->stringFlags = (uint16_t)((head->stringFlags & ~0x10) |
        (GetTextWidth((uint8_t *)s_viewHeader, (uint8_t *)_charTables[1]) + 8 <= head->dwWidth ? 0x10 : 0));
    sub->fg = (uintptr_t)s_viewSub;
    sub->isInvisible = classic;
    entry_at(RV_classic)->isInvisible = !classic;
    int32_t next = 0;                   /* (a group stage: the next of its lines to look at) */
    for (int32_t i = 0; i < kRows; i++) {
        const int32_t base = RV_rows + kCells * i;
        int32_t li = s_top + i;
        if (classic) {                  /* (the page's lines, in order, each where the layout put it) */
            while (next < s_nlines && s_lpage[next] != s_tpage)
                next++;
            li = next < s_nlines ? next++ : s_nlines;
            if (li < s_nlines)
                place_row(base, s_ly[li], CG_LINE);
        } else
            place_row(base, 32 + 11 * i, 10);
        const Line *l = li < s_nlines ? &s_lines[li] : 0;
        MenuEntry *line = entry_at(base), *left = entry_at(base + 1), *mid = entry_at(base + 2),
                  *right = entry_at(base + 3);
        const int32_t whole = l && (l->kind == L_TEXT || l->kind == L_HEAD);
        line->isInvisible = !whole;
        left->isInvisible = mid->isInvisible = right->isInvisible = 1;
        for (int32_t j = 0; j < 7; j++)
            entry_at(base + 4 + j)->isInvisible = 1;
        if (!l)
            continue;
        if (whole) {
            line->fg = (uintptr_t)l->a;
            line->stringFlags = (uint16_t)(l->kind == L_HEAD ? 0x0000 : 0x8000);
            continue;
        }
        left->isInvisible = 0;
        left->fg = (uintptr_t)l->a;
        if (l->kind == L_TIE) {
            mid->isInvisible = right->isInvisible = 0;
            mid->fg = (uintptr_t)l->b;
            right->fg = (uintptr_t)l->c;
            left->stringFlags = (uint16_t)(0x4000 | (l->win == 1 ? 15 : 2));    /* (the club through: yellow) */
            right->stringFlags = (uint16_t)(0x8000 | (l->win == 2 ? 15 : 2));
        } else {
            left->stringFlags = (uint16_t)(0x8000 | (l->kind != L_TABLE || !l->win ? 2 : l->win == 1 ? 13 : 14));
            for (int32_t j = 0; j < 7; j++) {
                MenuEntry *k = entry_at(base + 4 + j);
                k->isInvisible = !l->k[j][0];
                k->fg = (uintptr_t)l->k[j];
            }
        }
    }
    const int32_t pages = view_pages() > 1;
    entry_at(RV_scrollUp)->isInvisible = entry_at(RV_scrollDown)->isInvisible = !pages;
}

/* the cup's other rounds (PREV / NEXT ROUND): the showable ones in calendar order */
static int32_t step_round(int32_t dir, int32_t *c, int32_t *r)
{
    int32_t order[CT_NRD], k = 0, at = -1;
    const int32_t nr = rounds_of(s_vc);
    for (int32_t mo = 0; mo < 12; mo++)
        for (int32_t q = 0; q < nr; q++)
            if (cal_slot(R(s_vc, q)[9]) == mo && showable(s_vc, q)) {
                if (q == s_vr)
                    at = k;
                order[k++] = q;
            }
    const int32_t to = at + dir;
    if (at < 0 || to < 0 || to >= k)
        return 0;
    *c = s_vc;
    *r = order[to];
    return 1;
}

/* ---- the career club's European cup: its record (cup_record.c) ---- */

static void euro_stages(void)
{
    const CupRecordState *rec = CupRecord_Euro();
    s_nestage = 0;
    if (!rec)
        return;
    if (rec->nTable)
        s_estage[s_nestage++] = 0xFF;
    for (int32_t i = 0; i < rec->nGames && s_nestage < 8; i++) {
        int32_t seen = 0;
        for (int32_t k = 0; k < s_nestage; k++)
            seen |= s_estage[k] == rec->games[i].round;
        if (!seen)
            s_estage[s_nestage++] = rec->games[i].round;
    }
}

/* a knockout stage's ties: (first leg's home, away, first leg, second leg or NULL) */
typedef struct { uint16_t a, b; const CupRecGame *g[2]; } EuroTie;
static int32_t euro_ties(uint8_t stage, EuroTie *t, int32_t max)
{
    const CupRecordState *rec = CupRecord_Euro();
    int32_t n = 0;
    for (int32_t i = 0; rec && i < rec->nGames; i++) {
        const CupRecGame *g = &rec->games[i];
        if (g->round != stage)
            continue;
        int32_t k = -1;
        if (g->flags & CUPREC_LEG2)
            for (int32_t j = 0; j < n; j++)
                if (t[j].a == g->t2 && t[j].b == g->t1 && !t[j].g[1])
                    k = j;
        if (k >= 0)
            t[k].g[1] = g;
        else if (n < max) {
            t[n].a = g->t1;
            t[n].b = g->t2;
            t[n].g[0] = g;
            t[n].g[1] = 0;
            n++;
        }
    }
    return n;
}

static const char *team_name(uint16_t team)
{
    return word_name((u16)(team >> 8 | (team & 0xFF) << 8));
}

static const char *euro_stage_name(int32_t k)
{
    if (k < 0 || k >= s_nestage)
        return "";
    if (s_estage[k] == 0xFF)
        return "LEAGUE PHASE";
    EuroTie t[16];
    const int32_t n = euro_ties(s_estage[k], t, 16);
    const int32_t first = s_nestage && s_estage[0] == 0xFF ? k == 1 : k == 0;
    return n <= 1 ? "FINAL" : n <= 2 ? "SEMI-FINALS" : n <= 4 ? "QUARTER-FINALS" : first ? "PLAY-OFFS" : "ROUND OF 16";
}

static void euro_tie_line(const EuroTie *t)
{
    Line *l = add(L_TIE);
    snprintf(l->a, sizeof l->a, "%s", team_name(t->a));
    snprintf(l->c, sizeof l->c, "%s", team_name(t->b));
    const CupRecGame *g0 = t->g[0], *g1 = t->g[1], *dec = g1 ? g1 : g0;
    if (g1)
        snprintf(l->b, sizeof l->b, "%d-%d %d-%d", (int)g0->g1, (int)g0->g2, (int)g1->g2, (int)g1->g1);
    else
        snprintf(l->b, sizeof l->b, "%d-%d", (int)g0->g1, (int)g0->g2);
    if (dec->flags & CUPREC_PENS)
        snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), " P");
    else if (dec->flags & CUPREC_ET)
        snprintf(l->b + strlen(l->b), sizeof l->b - strlen(l->b), " ET");
    if ((dec->flags & CUPREC_DECIDED) && (g1 || !(g0->flags & CUPREC_LEG2))) {
        const uint16_t won = (dec->flags & CUPREC_T2WON) ? dec->t2 : dec->t1;
        l->win = won == t->a ? 1 : 2;
    }
}

static void euro_table_lines(void)
{
    const CupRecordState *rec = CupRecord_Euro();
    Line *h = add(L_TABHEAD);
    snprintf(h->a, sizeof h->a, "LEAGUE PHASE");
    snprintf(h->k[6], sizeof h->k[6], "PTS");
    for (int32_t i = 0; rec && i < rec->nTable; i++) {
        Line *l = add(L_TABLE);
        snprintf(l->a, sizeof l->a, "%d %s", (int)i + 1, team_name(rec->table[i]));
        put_num(l, 6, rec->points[i]);
        l->win = i < 8 ? 1 : i < 24 ? 2 : 0;   /* (1-8 the round of 16, 9-24 the play-offs) */
    }
}

static void euro_stage_lines(int32_t k)
{
    s_nlines = 0;
    s_top = 0;
    snprintf(s_viewHeader, sizeof s_viewHeader, "%s", s_euroName);
    snprintf(s_viewSub, sizeof s_viewSub, "%s", euro_stage_name(k));
    if (s_estage[k] == 0xFF) {
        euro_table_lines();
        return;
    }
    EuroTie t[16];
    const int32_t n = euro_ties(s_estage[k], t, 16);
    for (int32_t i = 0; i < n; i++)
        euro_tie_line(&t[i]);
}

/* STAT > SUMMARY's full page: the league phase, then every knockout stage played so far */
static void euro_summary_lines(void)
{
    s_nlines = 0;
    s_top = 0;
    snprintf(s_viewHeader, sizeof s_viewHeader, "%s", s_euroName);
    snprintf(s_viewSub, sizeof s_viewSub, "SUMMARY");
    for (int32_t k = 0; k < s_nestage; k++) {
        if (s_estage[k] == 0xFF) {
            euro_table_lines();
            add(L_TEXT);
            continue;
        }
        EuroTie t[16];
        const int32_t n = euro_ties(s_estage[k], t, 16);
        keep_together(2);
        text_line(L_HEAD, euro_stage_name(k));
        for (int32_t i = 0; i < n; i++)
            euro_tie_line(&t[i]);
        add(L_TEXT);
    }
    while (s_nlines && s_lines[s_nlines - 1].kind == L_TEXT && !s_lines[s_nlines - 1].a[0])
        s_nlines--;
    if (!s_nlines)
        text_line(L_TEXT, "NOTHING PLAYED YET");
}

static void contiRoundMenu_OnInit(void)
{
    int32_t c, r;
    s_ngt = s_nchunk = s_tpages = s_tpage = 0;
    if (s_euro) {
        euro_stages();
        if (s_ek >= 0 && s_ek < s_nestage)
            euro_stage_lines(s_ek);
        else
            euro_summary_lines();
    } else if (s_vc >= 0)
        build_round(s_vc, s_vr);
    else
        summary_lines();
    MenuEntry *prev = entry_at(RV_prev), *next = entry_at(RV_next);
    if (s_euro) {
        prev->isInvisible = s_ek <= 0;
        next->isInvisible = s_ek < 0 || s_ek + 1 >= s_nestage;
    } else {
        prev->isInvisible = s_vc < 0 || !step_round(-1, &c, &r);
        next->isInvisible = s_vc < 0 || !step_round(1, &c, &r);
    }
    const int32_t pages = view_pages() > 1;
    nav(RV_prev, -1, RV_exit, pages ? RV_scrollDown : -1, -1);
    nav(RV_exit, prev->isInvisible ? -1 : RV_prev, next->isInvisible ? -1 : RV_next, pages ? RV_scrollDown : -1, -1);
    nav(RV_next, RV_exit, -1, pages ? RV_scrollDown : -1, -1);
    nav(RV_scrollUp, -1, -1, -1, RV_scrollDown);
    nav(RV_scrollDown, -1, -1, RV_scrollUp, RV_exit);
    {   /* (PREV / NEXT ROUND to the cup's first / last round hides that button: the cursor goes to EXIT,
         *  not an empty frame) */
        extern uint8_t _currentMenu[];
        const Menu *menu = (const Menu *)(void *)_currentMenu;
        const MenuEntry *cur = menu ? menu->selectedEntry : 0;
        if (cur && cur->isInvisible && (cur->ordinal == RV_prev || cur->ordinal == RV_next))
            SetCurrentEntry(RV_exit);
    }
    refresh_view();
}
static void contiRoundMenu_OnDraw(void) { refresh_view(); }

/* a page on or back (a group stage: its own pages) */
static void page(int32_t dir)
{
    if (s_tpages) {
        if (s_tpage + dir >= 0 && s_tpage + dir < s_tpages)
            s_tpage += dir;
        return;
    }
    const int32_t to = s_top + dir * kRows;
    if (to >= 0 && to < s_nlines)
        s_top = to;
}
static void contiRoundMenu_scrollUp(void) { page(-1); }
static void contiRoundMenu_scrollDown(void) { page(1); }
static void other_round(int32_t dir)
{
    int32_t c, r;
    if (s_euro) {
        if (s_ek >= 0 && s_ek + dir >= 0 && s_ek + dir < s_nestage) {
            s_ek += dir;
            contiRoundMenu_OnInit();
        }
        return;
    }
    if (s_vc >= 0 && step_round(dir, &c, &r)) {
        s_vc = c;
        s_vr = r;
        contiRoundMenu_OnInit();
    }
}
static void contiRoundMenu_prev(void) { other_round(-1); }
static void contiRoundMenu_next(void) { other_round(1); }
static void contiRoundMenu_exit(void) { SetExitMenuFlag(); }

/* ---- registration, opening ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(contiConfsMenu_OnInit), HANDLER(contiConfsMenu_OnDraw),
    HANDLER(contiConfsMenu_conf0), HANDLER(contiConfsMenu_conf1), HANDLER(contiConfsMenu_conf2),
    HANDLER(contiConfsMenu_conf3), HANDLER(contiConfsMenu_conf4), HANDLER(contiConfsMenu_conf5),
    HANDLER(contiConfsMenu_exit),
    HANDLER(contiRoundsMenu_OnInit), HANDLER(contiRoundsMenu_OnDraw),
    HANDLER(contiRoundsMenu_round0), HANDLER(contiRoundsMenu_round1), HANDLER(contiRoundsMenu_round2),
    HANDLER(contiRoundsMenu_round3), HANDLER(contiRoundsMenu_round4), HANDLER(contiRoundsMenu_round5),
    HANDLER(contiRoundsMenu_round6), HANDLER(contiRoundsMenu_round7), HANDLER(contiRoundsMenu_round8),
    HANDLER(contiRoundsMenu_round9), HANDLER(contiRoundsMenu_round10), HANDLER(contiRoundsMenu_round11),
    HANDLER(contiRoundsMenu_round12), HANDLER(contiRoundsMenu_round13), HANDLER(contiRoundsMenu_round14),
    HANDLER(contiRoundsMenu_round15), HANDLER(contiRoundsMenu_summary), HANDLER(contiRoundsMenu_exit),
    HANDLER(contiRoundMenu_OnInit), HANDLER(contiRoundMenu_OnDraw),
    HANDLER(contiRoundMenu_prev), HANDLER(contiRoundMenu_exit), HANDLER(contiRoundMenu_next),
    HANDLER(contiRoundMenu_scrollUp), HANDLER(contiRoundMenu_scrollDown), HANDLER(contiRoundMenu_classic),
};
#undef HANDLER

void ContiView_Init(void)
{
    static int32_t registered;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
}

static void open(const char *name)
{
    ContiView_Init();
    uint8_t *menu = MenuJson_Load(name);
    if (!menu) {
        fprintf(stderr, "[conti-view] cannot show %s\n", name);
        return;
    }
    ShowMenu(menu);
}

static void show_rounds(int32_t conf)
{
    s_conf = conf;
    s_picked = -1;
    s_euro = 0;
    s_autoOpen = 1;
    open(CONTI_ROUNDS_MENU_NAME);
}

static void show_round(int32_t c, int32_t r)
{
    s_vc = c;
    s_vr = r;
    open(CONTI_ROUND_MENU_NAME);
}

static void show_stage(int32_t k)
{
    s_ek = k;
    open(CONTI_ROUND_MENU_NAME);
}

static void show_summary(void)
{
    s_ek = -1;
    s_vc = -1;
    open(CONTI_ROUND_MENU_NAME);
}

int32_t ContiView_Available(void)
{
    int32_t n = 0;
    return cups(&n) != 0 && n > 0;
}

void ContiView_ShowConfs(void)
{
    if (!ContiView_Available())
        return;
    s_only = -1;
    s_euro = 0;
    open(CONTI_CONFS_MENU_NAME);
}

int32_t ContiView_CupsOfContinent(uint8_t continent, uint8_t *out, int32_t max)
{
    int32_t n = 0, k = 0, f = -1;
    const WorldContiCup *t = cups(&n);
    for (int32_t i = 0; i < 6; i++)
        if (k_confContinent[i] == continent && continent)
            f = i;
    if (continent == 254 || continent == 255)   /* (the world: FIFA's) */
        f = WORLD_CONF_FIFA;
    for (int32_t c = 0; t && f >= 0 && c < n && k < max; c++)
        if (t[c].conf == f)
            out[k++] = (uint8_t)c;
    return k;
}

const char *ContiView_CupName(int32_t c)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    return t && c >= 0 && c < n ? t[c].name : "";
}

/* VIEW WORLD's row: the cup on its round being played (the first not played yet; after the
 * final: the last); none drawn: its SUMMARY. */
void ContiView_ShowCup(int32_t c)
{
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    if (!t || c < 0 || c >= n)
        return;
    snapshot();
    s_euro = 0;
    s_conf = t[c].conf;
    s_only = c;
    list_rounds();
    if (s_nround)
        show_round(s_round[s_sel] >> 4, s_round[s_sel] & 15);
    else
        show_summary();
    s_only = -1;
}

/* STAT > SUMMARY (world_winners.c, competitionStatsMenu entry 7) of the contest viewed: the career
 * club's European cup -> its phases (green boxes: the league phase, the knockout stages played so far)
 * and the full page; a conti cup's contest -> that cup's rounds. 0: none for this contest. */
static int32_t stat_cup(uint8_t id)
{
    extern uint8_t g_contiMainCup;
    if (id == COMPID_contiMain)
        return g_contiMainCup;
    if (id == COMPID_contiTie && CT_SEG.kind != 0xFF)
        return CT_SEG.cup;
    return -1;
}

int32_t ContiView_StatSummaryAvailable(void)
{
    extern DIYCompetitionEx _diyCompetitionStart;
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    const CupRecordState *rec = CupRecord_Euro();
    if (rec && rec->euroComp == id)
        return 1;
    int32_t n = 0;
    return stat_cup(id) >= 0 && cups(&n) && stat_cup(id) < n;
}

void ContiView_ShowStatSummary(void)
{
    extern DIYCompetitionEx _diyCompetitionStart;
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    const CupRecordState *rec = CupRecord_Euro();
    if (rec && rec->euroComp == id) {
        snprintf(s_euroName, sizeof s_euroName, "%.30s", (const char *)_diyCompetitionStart.base.competitionName);
        for (char *p = s_euroName; *p; p++)
            if (*p == '*')
                *p = 0;
        s_euro = 1;
        s_picked = -1;
        s_autoOpen = 0;
        open(CONTI_ROUNDS_MENU_NAME);
        s_euro = 0;
        return;
    }
    int32_t n = 0;
    const WorldContiCup *t = cups(&n);
    const int32_t c = stat_cup(id);
    if (!t || c < 0 || c >= n)
        return;
    snapshot();
    s_euro = 0;
    s_conf = t[c].conf;
    s_only = c;
    s_picked = -1;
    s_autoOpen = 0;
    open(CONTI_ROUNDS_MENU_NAME);
    s_only = -1;
}

/* --conti-view-selftest: the menus load and every round of every cup builds (on the edition the
 * save holds, if any) without running off its lines. */
int32_t ContiView_SelfTest(void)
{
    int32_t n = 0, bad = 0;
    const WorldContiCup *t = cups(&n);
    snapshot();
    printf("[conti-view] %d cups, an edition %s, week %d\n", (int)n, s_live ? "in progress" : "none", (int)s_week);
    for (int32_t c = 0; t && c < n; c++) {
        const int32_t nr = rounds_of(c);
        s_conf = t[c].conf;
        for (int32_t r = 0; r < nr; r++) {
            build_round(c, r);
            if (s_nlines >= kLinesMax)
                bad++;
        }
        summary_lines();
        printf("[conti-view] %-28s %s: %d rounds, summary %d lines\n", t[c].name, k_confName[t[c].conf], (int)nr,
               (int)s_nlines);
    }
    printf("[conti-view] %s\n", bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}

#else

int32_t ContiView_Available(void) { return 0; }
void ContiView_ShowConfs(void) {}
int32_t ContiView_CupsOfContinent(uint8_t continent, uint8_t *out, int32_t max)
{
    (void)continent; (void)out; (void)max;
    return 0;
}
const char *ContiView_CupName(int32_t c) { (void)c; return ""; }
void ContiView_ShowCup(int32_t c) { (void)c; }
void ContiView_Init(void) {}
int32_t ContiView_SelfTest(void) { return 0; }
int32_t ContiView_StatSummaryAvailable(void) { return 0; }
void ContiView_ShowStatSummary(void) {}

#endif
