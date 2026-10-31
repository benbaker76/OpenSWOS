/*
 * world_winners.c -- the SEASON WINNERS page (include/world_winners.h; the SWOS 96/97 Mod's
 * winners.py and gplay.c gp_wn_c, its titles fmtgame.win_lines' as scripts/world_import.py
 * bakes them: LeaguePlayoffs.winners).
 *
 * Each title of the viewed division is a block: its name and ':', then its winner (yellow), IN
 * PROGRESS or TO BE PLAYED, a blank line between blocks, five to a page; a title with nothing to
 * show this season is left out (a play-off without its contest: decided at the season end, so
 * shown TO BE PLAYED only in the career club's own league). Then, on a page of its own, the last
 * season: WINNERS 2026-27: and the titles of the division the career club played in, as its
 * season end decided them (g_leagueWinners). Its sources, as the Mod's:
 *   'L' hh dd   the table's 1st once it has played hh matches;
 *   'S' k hh dd stage table k's 1st once the league has played hh days (the stage bank);
 *   'K' pi      play-off pi's contest of the career club, its last tie's winner;
 *   'D' s pi ss stage title s decided mid-season, else as 'K' (in progress after ss days);
 *   'Q' r pi    a title_seed play-off, as 'K' -- or its champion when one club won both tournaments;
 *   'T' pi tt   tie tt's winner of play-off pi (the MLS's conference finals);
 *   'P' k       the Italian rules' contest of the career club (1, 2: the club relegated, 3);
 *   'C'         the state championship's winner.
 */
#include "world_winners.h"
#include "world.h"
#include "world_playoff.h"
#include "world_cups.h"
#include "league_playoffs.h"
#include "game_types.h"
#include "comp_identity.h"
#include "diy_competition.h"
#include "career_teams.h"
#include "menu_system.h"
#undef STR_COLOR_MASK                   /* (menu_data.h's enum names them too) */
#undef STR_FLAGS_MASK
#undef YELLOW_GREEN_BLACK_15
#include "menu_data.h"

/* Prevent GCC from converting byte loops to memcpy/memset (no libc on the Amiga) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

extern uint8_t  _seasonPlaying;
extern uint16_t _selTeamNumber;
extern uint16_t _gameType;
extern MenuEntry _menuEntries[];
extern TeamFileEx _careerTeam;
extern DIYCompetitionEx _diyCompetitionStart;

#define WN_LINES   14                   /* (fmtpage.LINES) */
#define WN_BLOCKS  5
#define WN_TEXT    48
#define WN_ALL     (8 * WN_LINES)       /* the pages' lines */
#define WN_YELLOW  15                   /* (fmtpage.WN_COLOUR: the winners' lines; titles white) */

static const char kTitle[] = "WINNERS ", kTbp[] = "TO BE PLAYED", kInc[] = "IN PROGRESS";

static char    s_text[WN_ALL][WN_TEXT];
static uint8_t s_colour[WN_ALL];
static int32_t s_nlines, s_page;
static char    s_header[24];
static char    s_lineBuf[WN_LINES][WN_TEXT];

/* ---- The viewed contest ---- */

static DIYCompetitionEx *viewed(void) { return &_diyCompetitionStart; }

static const LeaguePlayoffs *country_rules(uint8_t country)
{
    int32_t count = 0;
    const LeaguePlayoffs *all = World_LeaguePlayoffs(&count);
    for (int32_t i = 0; i < count; i++)
        if (all[i].country == country)
            return &all[i];
    return 0;
}

static const TeamFile *row_team(const DIYCompetitionEx *c, int32_t k)
{
    const DIYTeamStats *st = (const DIYTeamStats *)((const uint8_t *)&c->base +
                                                    c->matchData.league.teamEntryOffsets[k] + DIY_STANDINGS_OFFSET);
    return &((const TeamFileEx *)diy_resolve_team(st->teamSelOffset))->team;
}

static uint16_t row_played(const DIYCompetitionEx *c, int32_t k)
{
    const DIYTeamStats *st = (const DIYTeamStats *)((const uint8_t *)&c->base +
                                                    c->matchData.league.teamEntryOffsets[k] + DIY_STANDINGS_OFFSET);
    return st->gamesPlayed;
}

/* The viewed contest's titles: its country's rules, the division (0xFE a state championship). */
static const LeaguePlayoffs *viewed_titles(uint8_t *division)
{
    const DIYCompetitionEx *c = viewed();
    if (!World_Current() || _gameType != 4)
        return 0;
    if ((uint8_t)c->base.field_2E == COMPID_stateContest) {
        *division = 0xFE;
        return country_rules(World_FileCountry((uint8_t)_careerTeam.team.countryNumber));
    }
    if (c->base.competitionType != 0 || c->base.numTeams < 2)
        return 0;
    const TeamFile *t = row_team(c, 0);
    {   /* (its country's league, not a contest of its clubs: a continental cup's group) */
        extern void *_competitionsTable[];
        void **entry = (void **)_competitionsTable[t->countryNumber];
        const Competition *primary = entry ? (const Competition *)entry[0] : 0;
        if (!primary || (intptr_t)primary == -1 || (uint8_t)primary->compId != (uint8_t)c->base.field_2E)
            return 0;
    }
    *division = t->league;
    return country_rules(World_FileCountry(t->countryNumber));
}

static int32_t count_titles(const LeaguePlayoffs *lp, uint8_t division)
{
    int32_t n = 0;
    for (int32_t i = 0; lp && i < lp->nwinners; i++)
        n += lp->winners[i].division == division;
    return n;
}

/* A club of the viewed contest (its rows). */
static int32_t is_row(uint16_t team)
{
    const DIYCompetitionEx *c = viewed();
    for (int32_t k = 0; k < c->base.numTeams && k < 64; k++) {
        const TeamFile *t = row_team(c, k);
        if (PACK_BE_U16(t->countryNumber, t->teamOrdinal) == team)
            return 1;
    }
    return 0;
}

static const char *team_name(uint16_t team)
{
    const TeamFileEx *t = GetTeamPointer(team);
    return t ? (const char *)t->team.teamName : 0;
}

/* ---- Sources ---- */

static int32_t hex(const char *p) { return p[0] <= '9' ? p[0] - '0' : p[0] - 'A' + 10; }
static int32_t hex2(const char *p) { return hex(p) << 4 | hex(p + 1); }

static const LeagueStageBank *bank(uint8_t country)
{
    for (int32_t i = 0; i < LEAGUE_STAGE_BANKS; i++)
        if (g_leagueStages[i].used && g_leagueStages[i].key == country)
            return &g_leagueStages[i];
    return 0;
}

/* 'Q' without the career club's contest: the champion when one club has won both tournaments
 * (A = table 0, C = table 3 under rule 1, table 1 under rule 2), else none. */
static int32_t title_double(uint8_t country, int32_t rule, uint16_t *team)
{
    const LeagueStageBank *b = bank(country);
    const int32_t c = rule == 1 ? 3 : 1;
    if ((rule == 1 || rule == 2) && b && (b->fresh & 1) && (b->fresh >> c & 1) && b->n[0] && b->n[c] &&
        b->r[0][0].team == b->r[c][0].team) {
        *team = b->r[0][0].team;
        return 1;
    }
    return -1;
}

/* This season: the winner's name, kInc / kTbp, or 0 (no line). */
static const char *this_season(uint8_t country, const char *src)
{
    const DIYCompetitionEx *c = viewed();
    const char *none = kTbp;
    uint16_t team = 0;
    int32_t r = -1;
    switch (src[0]) {
    case 'L': {                                 /* (shown in any league: its table is live) */
        const uint16_t played = row_played(c, 0);
        if (played >= hex2(src + 1)) {
            const TeamFile *t = row_team(c, 0);
            return (const char *)t->teamName;
        }
        return played ? kInc : kTbp;
    }
    case 'C':
        if (c->base.winnerTeam != 0xFFFF)
            return team_name(c->base.winnerTeam);
        for (int32_t k = 0; k < c->base.numTeams && k < 64; k++)
            if (c->leagueStandings[k].gamesPlayed)
                return kInc;
        return kTbp;
    case 'P':
        r = WorldPlayoff_ItalianResult(hex(src + 1), &team);
        return r > 0 ? team_name(team) : r == 0 ? kInc : 0;
    case 'S': {
        const LeagueStageBank *b = bank(country);
        const int32_t k = hex(src + 1), days = hex2(src + 2);
        if (c->leagueMatchProgress >= days) {
            if (b && k < LEAGUE_STAGE_TABLES && (b->fresh >> k & 1) && b->n[k])
                return team_name(b->r[k][0].team);
        } else if (c->leagueMatchProgress)
            none = kInc;
        break;
    }
    case 'D': {
        const int32_t st = hex(src + 1), pi = hex2(src + 2), ss = hex2(src + 4);
        const WorldStageTitle *x = WorldPlayoff_StageTitle(st);
        if (x && x->done && x->pidx1 == pi + 1)
            return team_name(x->team[0]);
        r = WorldPlayoff_WinnerOf(pi, -1, &team);
        if (r < 0 && ss != 0xFF && c->leagueMatchProgress > ss)
            none = kInc;
        break;
    }
    case 'K':
        r = WorldPlayoff_WinnerOf(hex2(src + 1), -1, &team);
        break;
    case 'Q':
        r = WorldPlayoff_WinnerOf(hex2(src + 2), -1, &team);
        if (r < 0)
            r = title_double(country, hex(src + 1), &team);
        break;
    case 'T':
        r = WorldPlayoff_WinnerOf(hex2(src + 1), hex2(src + 3), &team);
        break;
    default:
        return 0;
    }
    if (r > 0)
        return team_name(team);
    if (r == 0)
        return kInc;
    return is_row(_selTeamNumber) ? none : 0;  /* (the career club's league: to be played) */
}

/* The last season (g_leagueWinners): its entry's winner, or 0. */
static const char *last_season(const char *src)
{
    const LeagueWinners *w = &g_leagueWinners;
    int32_t type, idx;
    switch (src[0]) {
    case 'K': type = 'K'; idx = hex2(src + 1); break;
    case 'D': type = 'K'; idx = hex2(src + 2); break;
    case 'Q': type = 'K'; idx = hex2(src + 2); break;
    case 'T': type = 0x80 | (hex2(src + 1) & 0x7F); idx = hex2(src + 3); break;
    case 'L': type = 'L'; idx = hex2(src + 3); break;
    case 'S': type = 'L'; idx = hex2(src + 4); break;
    default: return 0;
    }
    for (int32_t i = 0; i < w->n && i < LEAGUE_WINNERS_MAX; i++)
        if (w->e[i].type == type && w->e[i].idx == idx)
            return team_name(w->e[i].team);
    return 0;
}

static int32_t last_known(void)
{
    return g_leagueWinners.season && (uint8_t)(g_leagueWinners.season + 1) == (uint8_t)(g_worldCups.seasons + 1);
}

/* ---- The page's text ---- */

static void put(const char *a, const char *b, uint8_t colour)
{
    if (s_nlines >= WN_ALL)
        return;
    char *d = s_text[s_nlines];
    int32_t n = 0;
    while (a && *a && n < WN_TEXT - 2) d[n++] = *a++;
    while (b && *b && n < WN_TEXT - 1) d[n++] = *b++;
    d[n] = 0;
    s_colour[s_nlines++] = colour;
}

static void season_title(char *d, uint8_t season)
{
    uint32_t year = World_SeasonYear(season), n = 0;
    const char *p = kTitle;
    while (*p) d[n++] = *p++;
    d[n++] = (char)('0' + year / 1000); d[n++] = (char)('0' + year / 100 % 10);
    d[n++] = (char)('0' + year / 10 % 10); d[n++] = (char)('0' + year % 10);
    d[n++] = '-';
    d[n++] = (char)('0' + (year + 1) / 10 % 10); d[n++] = (char)('0' + (year + 1) % 10);
    d[n] = 0;
}

/* A block: its title and winner, on this page or (five blocks on it) the next. */
static int32_t s_blocks;
static void block(const char *title, const char *winner)
{
    if (s_blocks == WN_BLOCKS) {
        while (s_nlines % WN_LINES) put(0, 0, 0);
        s_blocks = 0;
    } else if (s_blocks)
        put(0, 0, 0);
    put(title, ":", 0);
    put(winner, 0, WN_YELLOW);
    s_blocks++;
}

static void build(void)
{
    uint8_t division = 0;
    const LeaguePlayoffs *lp = viewed_titles(&division);
    const DIYCompetitionEx *c = viewed();
    s_nlines = s_blocks = 0;
    if (!lp)
        return;
    for (int32_t i = 0; i < lp->nwinners; i++) {
        const LeagueWinLine *l = &lp->winners[i];
        if (l->division != division)
            continue;
        const char *w = this_season(lp->country, l->src);
        if (w)
            block(l->title ? l->title : (const char *)c->base.competitionName, w);
    }
    /* the last season, a page of its own: the division the career club played in ('M') */
    if (!last_known() || lp->country != World_FileCountry((uint8_t)_careerTeam.team.countryNumber))
        return;
    int32_t md = -1;
    for (int32_t i = 0; i < g_leagueWinners.n; i++)
        if (g_leagueWinners.e[i].type == 'M')
            md = g_leagueWinners.e[i].idx;
    if (md < 0)
        md = division;
    const int32_t at = s_nlines, blocks = s_blocks;
    if (s_nlines) {
        while (s_nlines % WN_LINES) put(0, 0, 0);
    }
    s_blocks = 0;
    char head[24];
    season_title(head, (uint8_t)(_seasonPlaying - 1));
    block(head, "");
    int32_t any = 0;
    for (int32_t i = 0; i < lp->nwinners; i++) {
        const LeagueWinLine *l = &lp->winners[i];
        if (l->division != md || !l->title)
            continue;
        const char *w = last_season(l->src);
        if (w) {
            block(l->title, w);
            any = 1;
        }
    }
    if (!any) {                                 /* (no title of the last season known: no such page) */
        s_nlines = at;
        s_blocks = blocks;
    }
}

/* ---- The menu ---- */

static void fill(void);
void WorldWinnersMenuInit(void) { fill(); }

static void prev_page(void)
{
    if (s_page > 0) {
        s_page--;
        fill();
        DrawMenu();
    }
}

void WorldWinnersNextPage(void)
{
    if ((s_page + 1) * WN_LINES < s_nlines) {
        s_page++;
        fill();
        DrawMenu();
    }
}

#pragma pack(push, 1)
typedef struct {
    MenuCoords16    rc;
    MenuCmd_TextPtr text;
    uint16_t        end;
} WinnersLine;

typedef struct {
    MenuHeader hdr;
    MenuMark   markByteCoords;
    MenuEntry_Byte_Colors_TextPtr title;
    MenuMark   markWideCoords;
    WinnersLine line[WN_LINES];
    MenuMark   markByteCoords2;
    MenuEntry_Byte_Nav_Colors_TextPtr_OnSelect prev, next, exit;
    MenuMark   markEndMenu;
} Menu_WorldWinners;
#pragma pack(pop)

static char s_aPrev[] = "PREV. PAGE", s_aNext[] = "NEXT PAGE", s_aExit[] = "EXIT";

#define WN_LINE(i) { { 0, (int16_t)(22 + 11 * (i)), 304, 9 }, { MENU_CMD_TEXT_PTR, 0x0, s_lineBuf[i] }, MENU_CMD_END }
#define WN_FIRST 1                      /* (the lines' entries; then PREV, NEXT, EXIT) */
#define WN_PREV  (WN_FIRST + WN_LINES)

static Menu_WorldWinners s_winnersMenu = {
    { WorldWinnersMenuInit, 0, 0, WN_PREV + 2 },
    MENU_MARK_BYTE_COORDS,
    {
        { 92, 0, 120, 15 },
        { MENU_CMD_COLORS, 0x17 },
        { MENU_CMD_TEXT_PTR, 0x0, s_header },
        MENU_CMD_END
    },
    MENU_MARK_WIDE_COORDS,
    {
        WN_LINE(0), WN_LINE(1), WN_LINE(2), WN_LINE(3), WN_LINE(4), WN_LINE(5), WN_LINE(6),
        WN_LINE(7), WN_LINE(8), WN_LINE(9), WN_LINE(10), WN_LINE(11), WN_LINE(12), WN_LINE(13),
    },
    MENU_MARK_BYTE_COORDS,
    {
        { 0, -71, 96, 15 },
        { MENU_CMD_NAV, -1, WN_PREV + 2, -1, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, s_aPrev },
        { MENU_CMD_ONSELECT_FIRE, prev_page },
        MENU_CMD_END
    },
    {
        { -48, -71, 96, 15 },
        { MENU_CMD_NAV, WN_PREV + 2, -1, -1, -1 },
        { MENU_CMD_COLORS, 0xE },
        { MENU_CMD_TEXT_PTR, 0x0, s_aNext },
        { MENU_CMD_ONSELECT_FIRE, WorldWinnersNextPage },
        MENU_CMD_END
    },
    {
        { 104, -71, 96, 15 },
        { MENU_CMD_NAV, WN_PREV, WN_PREV + 1, -1, -1 },
        { MENU_CMD_COLORS, 0xC },
        { MENU_CMD_TEXT_PTR, 0x0, s_aExit },
        { MENU_CMD_ONSELECT_FIRE, SetExitMenuFlag },
        MENU_CMD_END
    },
    MENU_MARK_END_MENU
};

/* The page's lines into its entries, PREV / NEXT shown when there is a page that way. */
static void fill(void)
{
    for (int32_t i = 0; i < WN_LINES; i++) {
        const int32_t at = s_page * WN_LINES + i;
        MenuEntry *e = &_menuEntries[WN_FIRST + i];
        char *d = (char *)e->fg;
        const char *src = at < s_nlines ? s_text[at] : "";
        int32_t n = 0;
        while (src[n] && n < WN_TEXT - 1) { d[n] = src[n]; n++; }
        d[n] = 0;
        e->stringFlags = (uint16_t)((e->stringFlags & ~STR_COLOR_MASK) | (at < s_nlines ? s_colour[at] : 0));
    }
    _menuEntries[WN_PREV].isInvisible = s_page == 0;
    _menuEntries[WN_PREV + 1].isInvisible = (s_page + 1) * WN_LINES >= s_nlines;
}

int32_t WorldWinners_Pages(void) { return (s_nlines + WN_LINES - 1) / WN_LINES; }

void CompetitionStatsWinners(void)
{
    build();
    season_title(s_header, _seasonPlaying);
    s_page = 0;
    ShowMenu((uint8_t *)&s_winnersMenu);
}

/* The statistics menu (menu_data.c _competitionStatsMenu): entry 5 WINNERS, between VIEW SQUADS
 * (3) and EXIT (4), hidden with the way round it when the viewed contest has no titles. */
#define STATS_SQUADS  3
#define STATS_EXIT    4
#define STATS_WINNERS 5
int32_t WorldWinners_Available(void)
{
    uint8_t division = 0;
    const LeaguePlayoffs *lp = viewed_titles(&division);
    return lp && count_titles(lp, division) > 0;
}

void CompetitionStatsInit(void)
{
    const int32_t on = WorldWinners_Available();
    _menuEntries[STATS_WINNERS].isInvisible = !on;
    _menuEntries[STATS_SQUADS].downEntry = on ? STATS_WINNERS : STATS_EXIT;
    _menuEntries[STATS_EXIT].upEntry = on ? STATS_WINNERS : STATS_SQUADS;
}

/* ---- The last season's record (league_playoffs.c writes it) ---- */

void WorldWinners_SeasonDecided(void)
{
    if (g_leagueWinners.fresh) {
        g_leagueWinners.season = (uint8_t)(g_worldCups.seasons + 1);
        g_leagueWinners.fresh = 0;
    }
}

void WorldWinners_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_leagueWinners;
    for (uint32_t i = 0; i < sizeof g_leagueWinners; i++)
        p[i] = 0;
}
