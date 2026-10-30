/*
 * conti_season.c -- the season end's glue of the other confederations' cups (src/world_conti.c,
 * include/world_conti.h; docs/WORLD_2026_27.md): the SWOS 96/97 Mod's tools/conti.py GLUE
 * (ct_ctry, ct_fill) on our season end.
 *
 * Every country of the season end, as it is processed: its top division's final table (as
 * team words), its top 9 and its cup winner's records (the grids the qualifiers read) -- the
 * first one finishing this season's edition first; every country seen: the next edition of
 * every cup (the career club, a seed of the season, the UEFA Champions League's holder for the
 * Intercontinental).
 *
 * Phase B, the career club's games (the Mod's ct_prep / ct_daily): a knockout tie (CT_SEG) is a
 * contest of one tie in slot 3 (CONTI_TIE_ID, _euroCupSlotIndex 7) under its cup's name, on its
 * round's months; when it is decided ct_result takes the result and the club's next tie takes
 * slot 3. Its cup's main round (CT_SEG kind 0x80) is the game's own contest (the Mod's conti.py
 * structs): a group stage and its last four knockout rounds as a tournament made from 96/97's
 * European Cup, a knockout main round as a cup made from its Cup Winners' Cup -- id 233 + the
 * cup, _euroCupSlotIndex 8 -- its clubs conti.c's draw (CT_LP). A loaded career rebuilds either
 * from the saved edition (ct_resume).
 *
 * The Mod's team words are file | ordinal << 8; ours BE {country, ordinal}: converted here (a
 * record's first two bytes read little-endian are the Mod's word already).
 */
#include "world_conti.h"
#include "league_playoffs.h"
#include "world.h"
#include "game_types.h"
#include "endian_helpers.h"
#include "diy_competition.h"

#if SWOS_WORLDS

extern uint8_t _primaryQualifiedTeamGrids[];
extern uint16_t _selTeamNumber, _isNationalSelector;
extern uint16_t _euroCupTitleHolderTeamId;
extern uint16_t _euroCupSlotIndex;
extern uint16_t _teamSelectionKeyCacheIndex, _teamSelectionKeyCacheOffset, _competitionSlotUpdatedFlag;
extern DIYCompetitionEx _diyCompetitionStart;
extern uint8_t *_careerCompetitionSlotPtrTable[];
uint16_t Career_SelectEuroCupSlotIfTeamParticipates(uint8_t *cupCopy, uint16_t euroCupIndex);
void Career_RetireSlotCalendarEntries(uint8_t slot);

static uint8_t s_on;

static u16 modWord(uint16_t team) { return (u16)((team >> 8) | (team << 8)); }
static uint16_t ourId(u16 w) { return (uint16_t)((w >> 8) | (w << 8)); }

#define CONTI_TIE_ID 232                /* (a free competition id: uefa_coef.c's ties are 229-231) */
#define CONTI_TIE_INDEX 7               /* (_euroCupSlotIndex: 0-2 the European cups, 3 the Copa, 4-6 UEFA ties) */
static CompetitionCup s_tie;
static uint16_t s_tieWinner;            /* (the career club's tie decided today: its winner) */
static uint8_t s_final;                 /* (the tie in slot 3 is its cup's final: a trophy) */
static uint8_t s_gplace;                /* (a group stage just ended: the career club's place) */
static uint8_t s_list[64], s_listOk;    /* (and its next stage's list, as the real draw) */

#define CONTI_MAIN_ID0 233              /* (233 + the cup: its main round's contest) */
#define CONTI_MAIN_INDEX 8              /* (_euroCupSlotIndex) */
static u16 s_lp[CT_NCUP][32];           /* (CT_LP: the main rounds' lists, as conti.c draws them) */
static CompetitionTournament s_groups;  /* (the career club's main round: a group stage) */
static CompetitionCup s_knockout;       /* (or a knockout round) */
static uint8_t s_rank;                  /* (its cup's rank in its confederation: the trophy's) */
extern CompetitionTournament _euroCup;
extern CompetitionCup _cupWinnersCup;

void Conti_Reset(void)
{
    uint8_t *p = (uint8_t *)&CT_ST;
    for (uint32_t i = 0; i < sizeof CT_ST; i++)
        p[i] = 0;
    p = (uint8_t *)&CT_ED;
    for (uint32_t i = 0; i < sizeof CT_ED; i++)
        p[i] = 0;
    CT_SEG.kind = 0xFF;
    s_on = 0;
    s_tieWinner = 0;
    s_gplace = 0;
    s_listOk = 0;
}

void Conti_Country(void)
{
    const u8 *spec = World_ContiSpec();
    if (!spec)
        return;
    CT_SPEC = spec;
    if (!s_on) {
        s_on = 1;
        ct_begin();
    }
    uint16_t words[32];
    const int32_t n = LeaguePlayoffs_TopTable(words, 32);
    for (int32_t i = 0; i < n; i++)
        words[i] = modWord(words[i]);
    ct_table_words(words, n);
    ct_country(_primaryQualifiedTeamGrids, _primaryQualifiedTeamGrids + 9 * sizeof(TeamFileEx));
}

/* The contests' lists for conti.c (the Mod's ct_lp): each cup's with a contest of its own. */
static void lists(void)
{
    int32_t ncups = 0;
    const WorldContiCup *cups = World_ContiCups(&ncups);
    for (int32_t c = 0; c < CT_NCUP; c++)
        CT_LP[c] = cups && c < ncups && cups[c].contest ? s_lp[c] : 0;
}

void Conti_SeasonEnd(void)
{
    if (!s_on)
        return;
    s_on = 0;
    lists();
    const uint8_t *g = _primaryQualifiedTeamGrids;  /* (a seed: the Mod's dword [STAND]) */
    const u32 seed = g[0] | g[1] << 8 | (u32)g[2] << 16 | (u32)g[3] << 24;
    const u16 hold = _euroCupTitleHolderTeamId && _euroCupTitleHolderTeamId != 0xFFFF
                   ? modWord(_euroCupTitleHolderTeamId) : 0xFFFF;
    ct_go(_isNationalSelector ? 0 : modWord(_selTeamNumber), seed, hold);
    SWOS_DBG("[conti] the next editions: %d clubs; winners of the one ended:", (int)CT_IN.nclub);
    for (int32_t c = 0; c < CT_NCUP; c++)
        if (CT_ST.hold[c].id)
            SWOS_DBG(" %d:%04X", (int)c, (unsigned)CT_ST.hold[c].id);
    SWOS_DBG("\n");
    if (CT_SEG.kind != 0xFF)
        SWOS_DBG("[conti] the career club's next game: cup %d round %d kind %02X, %d clubs (%04X v %04X), month %d\n",
                 (int)CT_SEG.cup, (int)CT_SEG.round, (unsigned)CT_SEG.kind, (int)CT_SEG.n,
                 (unsigned)ourId(CT_SEG.club[0]), (unsigned)ourId(CT_SEG.club[1]), (int)CT_SEG.month);
}

/* ---- phase B: the career club's ties ---- */

static int32_t spec(void)               /* (CT_SPEC: set at the season end; a loaded career's first use) */
{
    const u8 *sp = World_ContiSpec();
    if (!sp)
        return 0;
    CT_SPEC = sp;
    return 1;
}

static int32_t ours(void)               /* the edition's career club is the one managed */
{
    return CT_ED.mark == 0x4543 && CT_ED.mgr && !_isNationalSelector && CT_ED.mgr == modWord(_selTeamNumber);
}

/* CT_SEG a tie -> s_tie (the Mod's ct_prep): its cup's name, the two clubs in draw order, its
 * round byte and months; 0: not a tie. */
static uint8_t *make_tie(void)
{
    int32_t ncups = 0;
    const WorldContiCup *cups = World_ContiCups(&ncups);
    if (CT_SEG.kind != K_KO || CT_SEG.n != 2 || !cups || CT_SEG.cup >= ncups)
        return 0;
    uint8_t *z = (uint8_t *)&s_tie;
    for (uint32_t i = 0; i < sizeof s_tie; i++)
        z[i] = 0;
    s_final = CT_SEG.round < 8 && cups[CT_SEG.cup].round[CT_SEG.round] == 9;
    CompetitionCup *t = &s_tie;
    const uint16_t a = ourId(CT_SEG.club[0]), b = ourId(CT_SEG.club[1]);
    t->header.comp.compId = CONTI_TIE_ID;
    t->header.comp.type = 1;
    t->header.comp.teamNumber = -1;
    t->header.comp.beginMonth = (int8_t)CT_SEG.month;     /* (eighths of a month from January; 96 on: the next year) */
    t->header.comp.endMonth = (int8_t)(CT_SEG.month + 2);
    t->header.comp.reserved1 = 15;      /* (nameTableOffset, teamListOffset) */
    t->header.comp.reserved2 = 21;
    t->header.comp.seedReorderFlag = 1; /* (as drawn: the first club at home first) */
    t->header.teams = 2;
    t->header.awayGoalsMode = CT_SEG.level >= 2;  /* (away_pens, away_et_pens: the Mod's ct_away) */
    t->header.subsAndPointsForWin = 0x35;
    t->header.roundStringBase = 1;
    t->roundRuleFlags[0] = (int8_t)CT_SEG.legs;
    t->name[0] = (char *)cups[CT_SEG.cup].name;
    t->name[1] = (char *)cups[CT_SEG.cup].shortName;
    t->teamSeedList[0].countryNumber = (uint8_t)(a >> 8);
    t->teamSeedList[0].teamOrdinal = (uint8_t)a;
    t->teamSeedList[1].countryNumber = (uint8_t)(b >> 8);
    t->teamSeedList[1].teamOrdinal = (uint8_t)b;
    return (uint8_t *)t;
}

static u8 round_byte(const u8 *R)       /* (the game's: two legs, extra time as its level says, or one) */
{
    return R[1] == 2 ? (R[2] == 1 || R[2] == 3 ? 0x94 : 0x84) : 0x14;
}

static u8 month(u8 m)                   /* (eighths of a month from January; a season's Jan-Jul the next year) */
{
    return (u8)((m - 1 + (m < 8 ? 12 : 0)) * 8);
}

/* CT_SEG a main round -> its contest (the Mod's conti.structs, as the game's own): a group stage
 * and its last four knockout rounds from 96/97's European Cup (the stages; every round through
 * the game's own draw of the list's order; away goals if a round of the cup has them), a
 * knockout round and the rest from its Cup Winners' Cup; the clubs conti.c's draw (CT_LP). */
static int32_t main_round(int32_t c)    /* (its round of the game's contest, -1 none) */
{
    int32_t mr = -1;
    const u8 *Q;
    for (int32_t r = 0; (Q = ct_round_spec(c, r)) != 0; r++)
        if (Q[17])
            mr = r;
    return mr;
}

static uint8_t *make_main(int32_t c)
{
    int32_t ncups = 0;
    const WorldContiCup *cups = World_ContiCups(&ncups);
    const int32_t mr = main_round(c);
    if (!cups || c < 0 || c >= ncups || !cups[c].contest || mr < 0)
        return 0;
    const u8 *R = ct_round_spec(c, mr), *last = R, *Q;
    if (!R || !CT_LP[c])
        return 0;
    u8 ko[8];
    int32_t nko = 0, away = 0;
    for (int32_t r = 0; (Q = ct_round_spec(c, r)) != 0; r++)
        away |= Q[2] >= 2;
    for (int32_t r = mr + (R[0] == K_GROUPS); (Q = ct_round_spec(c, r)) != 0 && nko < 8; r++) {
        last = Q;
        if (Q[0] == K_KO)
            ko[nko++] = round_byte(Q);
    }
    const u8 begin = month(R[9]);
    u8 end = month(last[9]);
    s_rank = cups[c].rank;
    if (R[0] == K_GROUPS) {
        const int32_t ng = R[6], gs = R[7], n = ng * gs;
        const int32_t kn = nko > 4 ? 4 : nko, k0 = nko - kn;
        if (n < 2 || n > 32 || kn < 1)
            return 0;
        CompetitionTournament *t = &s_groups;
        *t = _euroCup;
        t->header.comp.compId = (int8_t)(CONTI_MAIN_ID0 + c);
        t->header.comp.beginMonth = (int8_t)begin;
        t->header.comp.endMonth = (int8_t)end;
        t->header.seedingType = 0;
        t->header.menuState = (int8_t)away;     /* (byte 0Ah: away goals after 90', the Mod's) */
        t->header.roundStringBase = (int8_t)(1 + kn);   /* (the stages) */
        uint8_t *z = (uint8_t *)t->header.rounds;
        for (uint32_t i = 0; i < sizeof t->header.rounds; i++)
            z[i] = 0;
        t->header.rounds[0].teamCount = (int8_t)n;
        t->header.rounds[0].groupCount = (int8_t)ng;
        t->header.rounds[0].teamGroupCount = (int8_t)gs;
        for (int32_t s = 0; s < kn; s++) {
            const int8_t m = (int8_t)(1 << (kn - s));
            t->header.rounds[1 + s].teamCount = m;
            t->header.rounds[1 + s].teamGroupCount = m;
        }
        t->header.rounds[1 + kn].teamCount = 1;
        t->header.reserved = 0;
        for (int32_t s = 0; s < 6; s++)
            t->roundRuleFlags[s] = (int8_t)(s >= 1 && s <= kn ? ko[k0 + s - 1] : 0);
        t->name[0] = (char *)cups[c].name;
        t->name[1] = (char *)cups[c].shortName;
        for (int32_t i = 0; i < 32; i++) {
            const uint16_t w = i < n ? ourId(CT_LP[c][i]) : 0;
            t->teamSeedList[i].countryNumber = (uint8_t)(w >> 8);
            t->teamSeedList[i].teamOrdinal = (uint8_t)w;
        }
        return (uint8_t *)t;
    }
    const int32_t n = 1 << nko;
    if (nko < 1 || nko > 5 || n > 32)
        return 0;
    CompetitionCup *k = &s_knockout;
    *k = _cupWinnersCup;
    k->header.comp.compId = (int8_t)(CONTI_MAIN_ID0 + c);
    if (end <= begin)                   /* (a one-round cup, the Recopa's: a month's window) */
        end = (u8)(begin + 8);
    k->header.comp.beginMonth = (int8_t)begin;
    k->header.comp.endMonth = (int8_t)end;
    k->header.teams = (int8_t)n;
    for (int32_t s = 0; s < 6; s++)
        k->roundRuleFlags[s] = (int8_t)(s < nko ? ko[s] : 0);
    k->name[0] = (char *)cups[c].name;
    k->name[1] = (char *)cups[c].shortName;
    for (int32_t i = 0; i < 32; i++) {
        const uint16_t w = i < n ? ourId(CT_LP[c][i]) : 0;
        k->teamSeedList[i].countryNumber = (uint8_t)(w >> 8);
        k->teamSeedList[i].teamOrdinal = (uint8_t)w;
    }
    return (uint8_t *)k;
}

/* CT_SEG's contest: a tie (index 7) or a main round (8); 0 none. */
static uint8_t *make_game(uint16_t *index)
{
    uint8_t *rec = make_tie();
    if (rec) {
        *index = CONTI_TIE_INDEX;
        return rec;
    }
    *index = CONTI_MAIN_INDEX;
    return CT_SEG.kind != 0xFF && (CT_SEG.kind & 0x80) ? make_main(CT_SEG.cup) : 0;
}

/* The slot-3 contest the career club starts a season in: its first tie or main round (0 = none). */
uint8_t *Conti_SeasonTie(uint16_t *index)
{
    if (!spec() || !ours())
        return 0;
    uint8_t *rec = make_game(index);
    if (rec)
        SWOS_DBG("[conti] season start: %s (contest %d)\n", *index == CONTI_TIE_INDEX ? "a tie" : "its main round",
                 (int)(uint8_t)((Competition *)rec)->compId);
    return rec;
}

/* A loaded career: the game under way again from the saved edition (the Mod's ct_pre). */
uint8_t *Conti_RestoreTie(uint16_t index)
{
    if ((index != CONTI_TIE_INDEX && index != CONTI_MAIN_INDEX) || !spec() || !ours())
        return 0;
    lists();
    ct_resume();
    if (index == CONTI_MAIN_INDEX) {    /* (its contest: the cup the loaded slot holds -- CT_SEG may be past
                                           its main round, its group place reported) */
        const uint8_t id = (uint8_t)((DIYCompetition *)_careerCompetitionSlotPtrTable[3])->field_2E;
        return id >= CONTI_MAIN_ID0 && id < CONTI_MAIN_ID0 + CT_NCUP ? make_main(id - CONTI_MAIN_ID0) : 0;
    }
    uint16_t now = 0;
    uint8_t *rec = make_game(&now);
    return now == index ? rec : 0;
}

/* Slot 3 takes rec mid-season: its calendar's entries so far retired first (the new entries all
 * come later: the position stays). */
static void into_slot3(uint8_t *rec, uint16_t index)
{
    SWOS_DBG("[conti] slot 3: %s (contest %d)\n", index == CONTI_TIE_INDEX ? "a tie" : "its main round",
             (int)(uint8_t)((Competition *)rec)->compId);
    Career_RetireSlotCalendarEntries(3);
    const uint16_t idx = _teamSelectionKeyCacheIndex, off = _teamSelectionKeyCacheOffset;
    Career_SelectEuroCupSlotIfTeamParticipates(rec, index);
    _teamSelectionKeyCacheIndex = idx;
    _teamSelectionKeyCacheOffset = off;
    _competitionSlotUpdatedFlag = 1;
}

/* A two-legged tie decided (diy_competition.c, as UefaQual_TieDecided): the career club's? */
void Conti_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    if ((uint8_t)_diyCompetitionStart.base.field_2E == CONTI_TIE_ID &&
        (team1 == _selTeamNumber || team2 == _selTeamNumber))
        s_tieWinner = team2Won ? team2 : team1;
}

/* A group stage's end in the game's contest (beside LeaguePhase_GroupsEnd; the Mod's ct_gpre /
 * ct_gend at lp36's lp_gend): before the game reshapes the contest, the career club's place in
 * its complete group if it moves the club to another cup (the Libertadores' 3rd to the
 * Sudamericana's play-off), else its place whatever it is; and, for a Libertadores-style round
 * of 16, the next stage's list as the real draw -- written after the game's own. The place goes
 * to ct_result on the day (Conti_Daily). */
void Conti_GroupsEnd(uint8_t *diy, int32_t after)
{
    const uint8_t id = (uint8_t)_diyCompetitionStart.base.field_2E;
    if (id < CONTI_MAIN_ID0 || id >= CONTI_MAIN_ID0 + CT_NCUP || !spec())
        return;
    if (after) {
        if (s_listOk) {
            uint8_t *list = ((DIYTournamentState *)diy)->teamIndexOrder;
            for (int32_t i = 0; i < 64; i++)
                if (s_list[i] != 0xFF)
                    list[i] = s_list[i];
        }
        s_listOk = 0;
        return;
    }
    DIYCompetitionEx *d = &_diyCompetitionStart;
    const int32_t ng = d->base.matchPairsPerRound, gs = d->base.totalMatchesInRound, n = ng * gs;
    if (n < 2 || n > 64)
        return;
    u8 row[64];
    u16 word[64], played[64], pts[64];
    for (int32_t i = 0; i < n; i++) {
        row[i] = (u8)(d->matchData.league.teamEntryOffsets[i] / 18);
        const DIYTeamStats *s = (const DIYTeamStats *)((uint8_t *)d + (uint32_t)row[i] * 18 + DIY_STANDINGS_OFFSET);
        const TeamFileEx *t = (const TeamFileEx *)diy_resolve_team(s->teamSelOffset);
        word[i] = modWord(PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal));
        played[i] = s->gamesPlayed;
        pts[i] = s->points;
    }
    const int32_t c = id - CONTI_MAIN_ID0;
    s_gplace = (uint8_t)ct_gplace(c, ng, gs, word, played);
    for (int32_t i = 0; i < 64; i++)
        s_list[i] = 0xFF;
    const u32 p = ct_gend(c, ng, gs, row, word, pts, s_list);
    s_listOk = 0;
    for (int32_t i = 0; i < 64; i++)
        s_listOk |= s_list[i] != 0xFF;
    if (!s_gplace && ours())
        s_gplace = (uint8_t)p;
}

/* The day just played (Career_AdvanceToNextTeamSelectionKeyCacheEntry; the Mod's ct_daily): the
 * career club's tie decided -> its result, the next tie into slot 3 (its calendar's entries so
 * far retired first). A main round is not played yet: the edition simulates it. */
void Conti_Daily(void)
{
    if (s_gplace) {                     /* a group stage ended: the club's place; a 3rd -> its next cup's
                                           tie (the Libertadores' to the Sudamericana's play-off), else the
                                           game's contest goes on (1st / 2nd: its knockouts; 4th: out) */
        const uint8_t place = s_gplace;
        s_gplace = 0;
        if (!spec() || !ours() || CT_SEG.kind == 0xFF)
            return;
        ct_result(place);
        SWOS_DBG("[conti] the career club's group: place %d; next: kind %02X (cup %d round %d)\n", (int)place,
                 (unsigned)CT_SEG.kind, (int)CT_SEG.cup, (int)CT_SEG.round);
        if (place == 3 && CT_SEG.kind == K_KO) {
            uint8_t *rec = make_tie();
            if (rec)
                into_slot3(rec, CONTI_TIE_INDEX);
        }
        return;
    }
    if (!s_tieWinner)
        return;
    const uint16_t winner = s_tieWinner;
    s_tieWinner = 0;
    if (!spec() || !ours() || CT_SEG.kind == 0xFF)
        return;
    if (CT_SEG.kind == K_KO) {          /* (a load since: CT_SEG again first) */
        ct_result(winner == _selTeamNumber);
        SWOS_DBG("[conti] the career club's tie (%s) %s; next: kind %02X (%04X v %04X)\n", s_tie.name[0],
                 winner == _selTeamNumber ? "won" : "lost", (unsigned)CT_SEG.kind,
                 (unsigned)ourId(CT_SEG.club[0]), (unsigned)ourId(CT_SEG.club[1]));
    }
    uint16_t index = 0;
    uint8_t *rec = make_game(&index);
    if (rec)                            /* (out: the finished tie stays in slot 3) */
        into_slot3(rec, index);
}

/* A tie's round name (world_cups.c): its cup's round, by key; -1: not a conti tie. */
int32_t Conti_RoundStringIndex(uint8_t compId)
{
    int32_t ncups = 0;
    const WorldContiCup *cups = World_ContiCups(&ncups);
    if (compId != CONTI_TIE_ID || !cups || CT_SEG.cup >= ncups || CT_SEG.round >= 8)
        return -1;
    return cups[CT_SEG.cup].round[CT_SEG.round];
}

/* The tie in slot 3 was its cup's final (the season's trophies, career_status.c). */
int32_t Conti_TieIsFinal(void) { return s_final; }

/* The trophy of the contest in slot 3 (_euroCupSlotIndex 7 / 8): -1 none (a tie before a final),
 * else its cup's rank in its confederation -- 0 as a Champions Cup, 1 as a Cup Winners' Cup, 2. */
int32_t Conti_TrophyTier(void)
{
    if (_euroCupSlotIndex == CONTI_TIE_INDEX)
        return s_final ? 1 : -1;
    return _euroCupSlotIndex == CONTI_MAIN_INDEX ? s_rank : -1;
}

/* The 96/97 Copa Libertadores gives way to a world's own cups. */
int32_t Conti_ReplacesCopa(void) { return World_ContiSpec() != 0; }

#else

void Conti_Country(void) { }
void Conti_SeasonEnd(void) { }
void Conti_Reset(void) { }
uint8_t *Conti_SeasonTie(uint16_t *index) { (void)index; return 0; }
uint8_t *Conti_RestoreTie(uint16_t index) { (void)index; return 0; }
void Conti_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won) { (void)team1; (void)team2; (void)team2Won; }
void Conti_Daily(void) { }
void Conti_GroupsEnd(uint8_t *diy, int32_t after) { (void)diy; (void)after; }
int32_t Conti_RoundStringIndex(uint8_t compId) { (void)compId; return -1; }
int32_t Conti_ReplacesCopa(void) { return 0; }
int32_t Conti_TieIsFinal(void) { return 0; }
int32_t Conti_TrophyTier(void) { return -1; }

#endif /* SWOS_WORLDS */
