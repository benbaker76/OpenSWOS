/*
 * mod_careerplus_rows.c -- Career+'s player rows (CAREER_PLUS.md F6), through playerRowDrawn
 * (docs/MODS.md §29): display only, no game state.
 *
 *  - a squad's SKILLS view: the seven ratings (H F S C T P V, 0-7 each) where the original shows
 *    the top three, and their total beside them; a goalkeeper has none ("-");
 *  - a squad's GOALS view: the player's nationality, "(ENG)";
 *  - PLAY MATCH's line-up: the total in the skills cell.
 *
 * The cells are the fork's (src/menus.c SquadAfterDrawCommon, play_match_menu.c
 * DrawPlayerEntries at its 8a6baf60): a squad row is entries 46-63, a line-up row 31-36.
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "game_types.h"

#include <stdio.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

enum {
    kSquadGoalsHeading = 13,    /* the squad screen's GOALS heading (SKILLS view) */
    kSquadHeading = 14,     /* its NAT heading: the new columns' */
    kSquadSkills  = 50,     /* a squad row's top-skills cell (46 + 4) */
    kSquadExtra   = 54,     /* its spare text cell, hidden by the original */
    kSquadGoals   = 55,     /* its goals cell */
    kSquadCompGoals = 59,   /* 59-63: the competitions' goals */
    kLineupSkills = 35,     /* a line-up row's skills cell (31 + 4) */
    kRows = 32              /* a squad's rows: a text of each per file slot */
};

/* The seven ratings in the fork's order (H F S C T P V), 0-7 each. 0 for a goalkeeper. */
static int32_t ratings(const PlayerFile *pf, uint8_t out[7])
{
    if ((pf->positionAndFace & PF_POSITION_MASK) == 0)
        return 0;
    out[0] = (uint8_t)pf->velocityHeading & 7;          /* heading */
    out[1] = (uint8_t)pf->speedFinishing & 7;           /* finishing */
    out[2] = ((uint8_t)pf->speedFinishing >> 4) & 7;    /* speed */
    out[3] = (uint8_t)pf->tacklingControl & 7;          /* control */
    out[4] = ((uint8_t)pf->tacklingControl >> 4) & 7;   /* tackling */
    out[5] = (uint8_t)pf->passing & 7;                  /* passing */
    out[6] = ((uint8_t)pf->velocityHeading >> 4) & 7;   /* velocity */
    return 1;
}

static int32_t total(const PlayerFile *pf)
{
    uint8_t r[7];
    int32_t t = 0;
    if (!ratings(pf, r))
        return -1;
    for (int32_t i = 0; i < 7; i++)
        t += r[i];
    return t;
}

enum { kSquadPosition = 49 };    /* the row's position cell: the new columns follow it */

/* A text cell at `left`, as wide as its text (the squad's cells centre their text in the cell;
 * a cell 0 wide centres it on x, which is how the original's top-skills cell sat). The right. */
static int32_t place_in(int32_t entry, const char *text, int32_t left, int32_t small)
{
    int32_t x, y, w, h;
    ModApi_MenuEntryRect(entry, &x, &y, &w, &h);
    w = ModApi_TextWidth(text, small);
    ModApi_SetMenuEntryRect(entry, left, y, w, h);
    return left + w;
}

static int32_t place(int32_t entry, const char *text, int32_t left)
{
    return place_in(entry, text, left, 1);  /* (a row's cells: the small font) */
}

static int32_t columns_left(void)
{
    int32_t x, y, w, h;
    ModApi_MenuEntryRect(kSquadPosition, &x, &y, &w, &h);
    return x + w + 4;
}

/* The column heading (entry 14, NAT), drawn before the rows: set on the first, drawn again. */
static void heading(const char *text, int32_t left, int32_t redraw)
{
    static const char *shown;
    int32_t x, y, w, h;
    ModApi_MenuEntryRect(kSquadHeading, &x, &y, &w, &h);
    ModApi_MenuText(kSquadHeading, text);
    ModApi_MenuVisible(kSquadHeading, 1);
    place_in(kSquadHeading, text, left, 1); /* (the headings: the small one) */
    if (shown != text || redraw) {
        shown = text;
        if (redraw)                         /* (SKILLS: the original's NAT, drawn there already) */
            ModApi_RestoreMenuBackground(x, y, w, h);
        ModApi_MenuRedraw(kSquadHeading);
    }
}

/* "H F S C T P V" over the seven digits: each letter padded with spaces until it stands where
 * its digit ("d d d ...") does (letters and digits differ in width; both the small font). */
static const char *ratings_heading(void)
{
    static char text[48];
    static const char kLetters[7] = { 'H', 'F', 'S', 'C', 'T', 'P', 'V' };
    if (text[0])
        return text;
    char digits[16] = "", probe[48];
    int32_t n = 0;
    for (int32_t i = 0; i < 7; i++) {
        const int32_t at = i ? ModApi_TextWidth(digits, 1) + ModApi_TextWidth(" ", 1) : 0;
        while (n > 0 && n < (int32_t)sizeof text - 2) {
            memcpy(probe, text, (size_t)n);
            probe[n] = 0;
            if (ModApi_TextWidth(probe, 1) >= at)
                break;
            text[n++] = ' ';
        }
        text[n++] = kLetters[i];
        text[n] = 0;
        strcat(digits, i ? " 0" : "0");
    }
    return text;
}

static void squad_row(const ModPlayerRow *row)
{
    static char skills[kRows][16], totals[kRows][8], nat[kRows][8];
    const int32_t ix = row->slot < kRows ? row->slot : (row->row & (kRows - 1));
    const PlayerFile *pf = row->player;
    const int32_t left = columns_left();
    if (ModApi_SquadGoalsView()) {
        char code[4];
        if (ModApi_NationalityCode((uint8_t)pf->nationality, code) != 0)
            return;
        snprintf(nat[ix], sizeof nat[ix], "(%s)", code);
        ModApi_MenuText(kSquadSkills, nat[ix]);
        place(kSquadSkills, nat[ix], left);
        if (row->row == 0)
            heading("NAT", left, 0);
        return;
    }
    uint8_t r[7];
    const int32_t outfield = ratings(pf, r);
    char *p = skills[ix];
    for (int32_t i = 0; i < 7; i++) {
        *p++ = outfield ? (char)('0' + r[i]) : '-';
        if (i != 6)
            *p++ = ' ';
    }
    *p = 0;
    ModApi_MenuText(kSquadSkills, skills[ix]);
    const int32_t right = place(kSquadSkills, skills[ix], left);
    if (outfield)
        snprintf(totals[ix], sizeof totals[ix], "(%02d)", (int)total(pf));
    else
        snprintf(totals[ix], sizeof totals[ix], "(-)");
    ModApi_MenuText(kSquadExtra, totals[ix]);
    place(kSquadExtra, totals[ix], right + 5);
    ModApi_MenuVisible(kSquadExtra, 1);
    ModApi_MenuVisible(kSquadGoals, 0);             /* (its place: the total) */
    for (int32_t e = kSquadCompGoals; e < kSquadCompGoals + 5; e++)
        ModApi_MenuVisible(e, 0);
    if (row->row == 0) {
        ModApi_MenuVisible(kSquadGoalsHeading, 0);  /* (first: drawing it hidden clears its place) */
        ModApi_MenuRedraw(kSquadGoalsHeading);
        heading(ratings_heading(), left, 1);
    }
}

static void lineup_row(const ModPlayerRow *row)
{
    static char totals[kRows][8];
    const int32_t ix = row->row >= 0 ? row->row & (kRows - 1) : 0;
    const int32_t t = total(row->player);
    if (t < 0)
        snprintf(totals[ix], sizeof totals[ix], "-");
    else
        snprintf(totals[ix], sizeof totals[ix], "%02d", (int)(t & 63));
    ModApi_MenuText(kLineupSkills, totals[ix]);
}

/* F2's ages: "NAME (age)" in a squad's name cell (48), "SURNAME (age)" in a transfer screen's
 * (its fourth: the game's ExtractSurname), where the row says whose player he is. */
enum { kSquadName = 48, kMarketName = 3 };

static void squad_age(const ModPlayerRow *row)
{
    static char text[kRows][32];
    if (row->team == 0xFFFF || row->slot >= 32)
        return;
    snprintf(text[row->slot], sizeof text[row->slot], "%.22s (%d)", (const char *)row->player->playerName,
             (int)CP_Age(row->team, row->slot));
    ModApi_MenuText(kSquadName, text[row->slot]);
}

static void market_age(const ModPlayerRow *row)
{
    extern uint8_t *ExtractSurname(uint8_t *name, uint16_t maxLenAndFlags);
    static char text[64][32];
    static int32_t next;
    if (row->team == 0xFFFF || row->slot >= 32 || row->cells <= kMarketName)
        return;
    const char *surname = (const char *)ExtractSurname((uint8_t *)row->player->playerName, 0x0D + 0x10);
    char *t = text[next++ & 63];            /* (each row's own: a screen's rows are drawn in turn) */
    snprintf(t, sizeof text[0], "%.22s (%d)", surname, (int)CP_Age(row->team, row->slot));
    ModApi_MenuText(row->firstCell + kMarketName, t);
}

void CP_Rows_Drawn(const ModPlayerRow *row)
{
    if (!row->player)
        return;
    if (row->screen == MODROWS_SQUAD) {
        squad_row(row);
        squad_age(row);
    } else if (row->screen == MODROWS_PLAY_MATCH)
        lineup_row(row);
    else if (row->screen == MODROWS_MARKET)
        market_age(row);
}

#endif /* SWOS_SDL_BUILD */
