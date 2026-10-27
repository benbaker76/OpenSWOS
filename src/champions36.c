/* champions36.c — the 36-team european presets (modern UEFA formats).
 *
 * PRESET COMPETITION -> EUROPE substitutes a 36-team single-league tournament
 * record over each of the three legacy euro-cup records:
 *
 *   EUROPEAN CHAMPIONS CUP  -> CHAMPIONS LEAGUE   (compId 129)
 *   EUFA CUP                -> EUROPA LEAGUE      (compId 130)
 *   EUROPEAN CUP-WINNERS CUP-> CONFERENCE LEAGUE  (compId 131)
 *
 * Format (identical for the three): one 36-club table, 8 games each (4 home,
 * 4 away), ranks 1-8 bye to the last 16, ranks 9-24 play two-leg play-offs,
 * 25-36 out; then two-leg knockouts (ET + pens, no away goals) to a one-leg
 * final. The club tiers are curated by the game's own strength metric (sum of
 * the 16 players' price bytes): the Champions League takes the top 4 of
 * Italy/England/Germany/Spain/France, the top 2 of Portugal/Holland, and the
 * 12 strongest champions of the other leagues; the Europa and Conference
 * League take the strongest remaining clubs tier by tier (max 3 per country).
 * Regenerate with the ranking script if the team database changes
 * (docs/CHAMPIONS36.md).
 *
 * The legacy records, their career copies and the JSON competition data are
 * never touched. SEASON mode claims its slot-3 euro cup with these records
 * (curated field), and CAREER mode claims with Champions36_CareerRecord —
 * the same record re-seeded from that season's QUALIFIED field, topped up
 * from the curated tiers (docs/CHAMPIONS36.md "Career mode"); the career's
 * qualification pipeline and its background cups stay on the legacy records.
 * The substitutes carry their own compIds (129..131), which is how the
 * modern-format hooks recognize their DIY buffers — by id, never by name, so
 * the legacy cups can share the renamed display strings safely. */
#include "champions36.h"
#include "diy_competition.h"
#include "comp_identity.h"
#include "endian_helpers.h"
#include <string.h>
#ifdef SWOS_SDL_BUILD
#include "save_format_json.h"
#include "swos_io.h"
#include <stdio.h>
#include <time.h>
#endif

extern CompetitionTournament _euroCup;
extern CompetitionCup _cupWinnersCup;
extern CompetitionCup _uefaCup;

/* Keep the legacy 32-seed records (and all career copy sizes) intact. A
 * substitute has the same header/name/seed offsets and its own 36-entry seed
 * storage. */
typedef struct __attribute__((packed)) {
    CompTournamentHeader header;
    int8_t roundRuleFlags[6];
    char *name[2];
    TeamId teamSeedList[36];
} Preset36;

_Static_assert(offsetof(Preset36, teamSeedList) == offsetof(CompetitionTournament, teamSeedList),
               "preset seed offset must match the legacy record reader");

static char kChampionsTitle[]  = "CHAMPIONS LEAGUE";
static char kEuropaTitle[]     = "EUROPA LEAGUE";
static char kConferenceTitle[] = "CONFERENCE LEAGUE";

/* Top 4 of the big five leagues, top 2 of Portugal/Holland, then the 12
 * strongest champions of the remaining leagues. */
static const TeamId kChampionsSeeds[36] = {
    { CN_ITALY, 0 /* AC MILAN */ },
    { CN_ITALY, 22 /* JUVENTUS */ },
    { CN_ITALY, 31 /* PARMA */ },
    { CN_ITALY, 23 /* LAZIO */ },
    { CN_ENGLAND, 20 /* MANCHESTER UTD */ },
    { CN_ENGLAND, 22 /* NEWCASTLE UNITED */ },
    { CN_ENGLAND, 18 /* LIVERPOOL */ },
    { CN_ENGLAND, 1 /* ASTON VILLA */ },
    { CN_GERMANY, 4 /* BOR. DORTMUND */ },
    { CN_GERMANY, 3 /* BAYERN MUNICH */ },
    { CN_GERMANY, 39 /* VFB STUTTGART */ },
    { CN_GERMANY, 44 /* WERDER BREMEN */ },
    { CN_SPAIN, 8 /* BARCELONA */ },
    { CN_SPAIN, 35 /* REAL MADRID */ },
    { CN_SPAIN, 16 /* DEP. LA CORUNA */ },
    { CN_SPAIN, 6 /* ATLETICO MADRID */ },
    { CN_FRANCE, 30 /* PARIS ST-GERMAIN */ },
    { CN_FRANCE, 23 /* MONACO */ },
    { CN_FRANCE, 1 /* AUXERRE */ },
    { CN_FRANCE, 19 /* LYON */ },
    { CN_PORTUGAL, 12 /* FC PORTO */ },
    { CN_PORTUGAL, 20 /* SPORTING LISBON */ },
    { CN_HOLLAND, 0 /* AJAX */ },
    { CN_HOLLAND, 21 /* PSV */ },
    { CN_SCOTLAND, 33 /* RANGERS */ },
    { CN_BELGIUM, 0 /* ANDERLECHT */ },
    { CN_CZECH_REPUBLIC, 8 /* SLAVIA PRAGUE */ },
    { CN_RUSSIA, 16 /* SPARTAK MOSCOW */ },
    { CN_AUSTRIA, 5 /* RAPID VIENNA */ },
    { CN_ROMANIA, 18 /* STEAUA BUCHAREST */ },
    { CN_GREECE, 17 /* OLYMPIAKOS */ },
    { CN_SWEDEN, 8 /* IFK GOTHENBURG */ },
    { CN_TURKEY, 9 /* FENERBAHCE */ },
    { CN_HUNGARY, 5 /* FERENCVAROS */ },
    { CN_SWITZERLAND, 5 /* GRASSHOPPERS */ },
    { CN_UKRAINE, 2 /* DINAMO KIEV */ },
};

/* The strongest clubs left after the Champions League picks, max 3 per
 * country. */
static const TeamId kEuropaSeeds[36] = {
    { CN_ENGLAND, 4 /* BLACKBURN ROVERS */ },
    { CN_ENGLAND, 0 /* ARSENAL */ },
    { CN_ITALY, 21 /* INTER MILAN */ },
    { CN_ENGLAND, 12 /* EVERTON */ },
    { CN_ITALY, 40 /* ROMA */ },
    { CN_ITALY, 18 /* FIORENTINA */ },
    { CN_SPAIN, 44 /* VALENCIA */ },
    { CN_HOLLAND, 10 /* FEYENOORD */ },
    { CN_SCOTLAND, 9 /* CELTIC */ },
    { CN_PORTUGAL, 2 /* BENFICA */ },
    { CN_SPAIN, 10 /* BETIS */ },
    { CN_GERMANY, 2 /* BAYER LEVERKUSEN */ },
    { CN_SPAIN, 47 /* ZARAGOZA */ },
    { CN_GERMANY, 23 /* KARLSRUHE */ },
    { CN_BELGIUM, 4 /* CLUB BRUGES */ },
    { CN_GERMANY, 25 /* MONCHENGLADBACH */ },
    { CN_BELGIUM, 17 /* STANDARD LIEGE */ },
    { CN_SCOTLAND, 0 /* ABERDEEN */ },
    { CN_CZECH_REPUBLIC, 10 /* SPARTA PRAGUE */ },
    { CN_FRANCE, 4 /* BORDEAUX */ },
    { CN_FRANCE, 27 /* NANTES */ },
    { CN_FRANCE, 20 /* MARSEILLE */ },
    { CN_HOLLAND, 31 /* VITESSE */ },
    { CN_BELGIUM, 1 /* ANTWERP */ },
    { CN_SCOTLAND, 15 /* DUNDEE UNITED */ },
    { CN_HOLLAND, 28 /* TWENTE */ },
    { CN_AUSTRIA, 8 /* SV SALZBURG */ },
    { CN_GREECE, 0 /* AEK ATHENS */ },
    { CN_GREECE, 19 /* PANATHINAIKOS */ },
    { CN_TURKEY, 21 /* TRABZONSPOR */ },
    { CN_TURKEY, 5 /* BESIKTAS */ },
    { CN_TURKEY, 10 /* GALATASARAY */ },
    { CN_AUSTRIA, 9 /* TIROL INNSBRUCK */ },
    { CN_AUSTRIA, 7 /* STURM GRAZ */ },
    { CN_POLAND, 7 /* LEGIA WARSAW */ },
    { CN_CROATIA, 2 /* HAJDUK SPLIT */ },
};

/* The tier below that. */
static const TeamId kConferenceSeeds[36] = {
    { CN_ENGLAND, 38 /* TOTTENHAM H. */ },
    { CN_ENGLAND, 8 /* CHELSEA */ },
    { CN_ENGLAND, 16 /* LEEDS UNITED */ },
    { CN_ITALY, 42 /* SAMPDORIA */ },
    { CN_ITALY, 28 /* NAPOLI */ },
    { CN_SPAIN, 42 /* TENERIFE */ },
    { CN_GERMANY, 17 /* HAMBURG */ },
    { CN_SPAIN, 41 /* SPORTING GIJON */ },
    { CN_SPAIN, 4 /* ATHLETIC BILBAO */ },
    { CN_GERMANY, 7 /* COLOGNE */ },
    { CN_ITALY, 47 /* UDINESE */ },
    { CN_GERMANY, 22 /* KAISERSLAUTERN */ },
    { CN_FRANCE, 37 /* STRASBOURG */ },
    { CN_FRANCE, 33 /* RENNES */ },
    { CN_FRANCE, 5 /* CAEN */ },
    { CN_HOLLAND, 24 /* RODA JC */ },
    { CN_BELGIUM, 3 /* CHARLEROI */ },
    { CN_BELGIUM, 7 /* GENT */ },
    { CN_SCOTLAND, 24 /* HIBERNIAN */ },
    { CN_PORTUGAL, 3 /* BOAVISTA */ },
    { CN_SCOTLAND, 23 /* HEARTS */ },
    { CN_AUSTRIA, 0 /* AUSTRIA VIENNA */ },
    { CN_BELGIUM, 10 /* KV MECHELEN */ },
    { CN_HOLLAND, 18 /* MVV */ },
    { CN_NORWAY, 11 /* ROSENBORG */ },
    { CN_HOLLAND, 13 /* GRONINGEN */ },
    { CN_AUSTRIA, 4 /* NIEDEROSTERREICH */ },
    { CN_PORTUGAL, 24 /* VIT. GUIMARAES */ },
    { CN_SCOTLAND, 28 /* MOTHERWELL */ },
    { CN_YUGOSLAVIA, 7 /* RED STAR B'GRADE */ },
    { CN_SWEDEN, 15 /* OSTER */ },
    { CN_DENMARK, 9 /* SILKEBORG */ },
    { CN_PORTUGAL, 11 /* FARENSE */ },
    { CN_ISRAEL, 14 /* MACCABI TEL-AVIV */ },
    { CN_BULGARIA, 14 /* SLAVIA SOFIA */ },
    { CN_SWITZERLAND, 10 /* NEUCHATEL XAMAX */ },
};

static const struct {
    const void   *original;    /* the legacy preset record this replaces */
    uint8_t       compId;      /* COMPID_championsLeague..conferenceLeague */
    char         *title;
    const TeamId *seeds;
} k_tiers[3] = {
    { &_euroCup,       COMPID_championsLeague,  kChampionsTitle,  kChampionsSeeds },
    { &_uefaCup,       COMPID_europaLeague,     kEuropaTitle,     kEuropaSeeds },
    { &_cupWinnersCup, COMPID_conferenceLeague, kConferenceTitle, kConferenceSeeds },
};

static Preset36 s_presets[3];

uint8_t *Champions36_PresetRecord(uint8_t *record)
{
    for (unsigned t = 0; t < 3; ++t) {
        if (record != (uint8_t *)k_tiers[t].original)
            continue;
        Preset36 *p = &s_presets[t];
        memset(p, 0, sizeof *p);
        Competition *c = &p->header.comp;
        c->compId = k_tiers[t].compId;
        c->type = 2;                 /* tournament */
        c->teamNumber = -1;          /* clubs resolve from TEAM.<country> */
        c->beginMonth = 64;          /* SEP..MAY, x8 encoded (as the euro cups) */
        c->endMonth = 32;
        c->reserved1 = 35;           /* legacy self-offset bytes, inert */
        c->reserved2 = 41;
        c->seedReorderFlag = 1;      /* keep the seed order; the per-run entry
                                      * shuffle still randomizes the fixtures */
        p->header.seedingType = 1;
        p->header.menuState = 0;     /* no away-goals rule in knockout ties */
        p->header.playEachTeam = 1;
        p->header.awayGoalsMode = 3; /* league: points for a win */
        p->header.subsAndPointsForWin = 0x35;
        p->header.roundStringBase = 6; /* the record loader's STAGE COUNT */
        p->header.reserved = 1;      /* with six stages the loader peeks past
                                      * rounds[5] for the final's qualifier
                                      * count — byte 33 is that count */
        static const Round rounds[6] = {{36,1,36}, {16,0,16}, {16,0,16},
                                        {8,0,8}, {4,0,4}, {2,0,2}};
        memcpy(p->header.rounds, rounds, sizeof rounds);
        /* Two legs + extra time + penalties, then a single neutral final. */
        static const int8_t rules[6] = {0, (int8_t)0x94, (int8_t)0x94,
                                        (int8_t)0x94, (int8_t)0x94, 0x14};
        memcpy(p->roundRuleFlags, rules, sizeof rules);
        p->name[0] = k_tiers[t].title;
        p->name[1] = k_tiers[t].title;
        memcpy(p->teamSeedList, k_tiers[t].seeds, sizeof p->teamSeedList);
        return (uint8_t *)p;
    }
    return record;
}

/* SEASON enrolment: which tier's curated field holds this club. teamId is the
 * BE-packed (country, ordinal) word used everywhere (TEAMID_PACK form). */
int Champions36_TierForTeam(uint16_t teamId)
{
    for (unsigned t = 0; t < 3; ++t)
        for (unsigned i = 0; i < 36; ++i)
            if (TEAMID_PACK(k_tiers[t].seeds[i]) == teamId)
                return (int)t;
    return -1;
}

uint8_t *Champions36_TierRecord(unsigned tier)
{
    if (tier >= 3)
        return 0;
    return Champions36_PresetRecord((uint8_t *)k_tiers[tier].original);
}

uint8_t *Champions36_TierRecordIdentity(unsigned tier)
{
    return tier < 3 ? (uint8_t *)&s_presets[tier] : 0;
}

/* CAREER enrolment: the tier's record with this season's QUALIFIED field.
 * The lists are BE-packed {country, ordinal} byte pairs straight out of the
 * legacy *Copy records (career_init.c's euroCupCopyTeamList). */
static int idInBEList(uint16_t id, const uint8_t *list, uint16_t count)
{
    for (uint16_t i = 0; i < count; i++)
        if (BE_READ_U16(list + 2u * i) == id)
            return 1;
    return 0;
}

uint8_t *Champions36_CareerRecord(unsigned tier,
                                  const uint8_t *qualified, uint16_t qualifiedCount,
                                  const uint8_t *otherA, uint16_t otherACount,
                                  const uint8_t *otherB, uint16_t otherBCount)
{
    if (tier >= 3)
        return 0;
    Preset36 *p = (Preset36 *)Champions36_PresetRecord((uint8_t *)k_tiers[tier].original);
    unsigned n = 0;

    /* This season's qualifiers first, deduped: the legacy UEFA fill can
     * inject the player's club into more than one country berth. */
    for (uint16_t i = 0; i < qualifiedCount && n < 36; i++) {
        uint16_t id = BE_READ_U16(qualified + 2u * i);
        unsigned dup = 0;
        for (unsigned j = 0; j < n && !dup; j++)
            if (TEAMID_PACK(p->teamSeedList[j]) == id)
                dup = 1;
        if (dup)
            continue;
        p->teamSeedList[n].countryNumber = qualified[2u * i];
        p->teamSeedList[n].teamOrdinal   = qualified[2u * i + 1];
        n++;
    }

    /* Top up to 36 from the curated tiers, own tier first, skipping clubs
     * already seeded or contesting another euro cup this season. The three
     * qualified fields hold at most 80 clubs and the curated pool 108
     * distinct ones, so the top-up cannot run dry (CL pads 20, EL/Conf 4). */
    for (unsigned t = 0; t < 3 && n < 36; t++) {
        const TeamId *curated = k_tiers[(tier + t) % 3].seeds;
        for (unsigned i = 0; i < 36 && n < 36; i++) {
            uint16_t id = TEAMID_PACK(curated[i]);
            unsigned taken = idInBEList(id, qualified, qualifiedCount) ||
                             idInBEList(id, otherA, otherACount) ||
                             idInBEList(id, otherB, otherBCount);
            for (unsigned j = 0; j < n && !taken; j++)
                if (TEAMID_PACK(p->teamSeedList[j]) == id)
                    taken = 1;
            if (!taken)
                p->teamSeedList[n++] = curated[i];
        }
    }

    return n == 36 ? (uint8_t *)p : 0;
}

int Champions36_Is(const DIYCompetition *competition)
{
    /* field_2E is the DIY buffer's compId: 129..131 are the substitutes' own
     * ids, which nothing else produces, and they serialize with the buffer —
     * a stable format marker on both targets, immune to the display names
     * (the legacy euro cups share those since the rename). */
    return competition->field_2E >= COMPID_championsLeague &&
           competition->field_2E <= COMPID_conferenceLeague;
}

int Champions36_IsLeague(const DIYCompetition *competition)
{
    return Champions36_Is(competition) && competition->competitionType == 0 &&
        competition->numTeams == 36;
}

/* Eight rounds of the circle method. Orient each union of two rounds as
 * cycles: every club then has exactly one home and one away game per pair
 * of matchdays. All fixtures are reproducible from their index after load;
 * no transient draw state is needed. UEFA pot/country constraints are not
 * imposed in this proof of concept. */
void Champions36_Fixture(unsigned index, uint8_t *home, uint8_t *away)
{
    uint8_t opponents[2][36], orientation[36] = {0};
    unsigned round = index / 18, pair = index % 18;
    unsigned first = round & ~1u;
    for (unsigned r = 0; r < 2; ++r) {
        unsigned rotation = first + r;
        for (unsigned p = 0; p < 18; ++p) {
            unsigned a = p == 0 ? 35 : (p + rotation) % 35;
            unsigned b = (35 - p + rotation) % 35;
            opponents[r][a] = (uint8_t)b;
            opponents[r][b] = (uint8_t)a;
        }
    }
    for (unsigned start = 0; start < 36; ++start) {
        if (orientation[start]) continue;
        unsigned club = start, r = 0;
        do {
            orientation[club] = (uint8_t)(r + 1);
            club = opponents[r][club];
            r ^= 1;
        } while (club != start);
    }
    unsigned a = pair == 0 ? 35 : (pair + round) % 35;
    unsigned b = opponents[round & 1][a];
    if (orientation[a] == (round & 1) + 1) {
        *home = (uint8_t)a; *away = (uint8_t)b;
    } else {
        *home = (uint8_t)b; *away = (uint8_t)a;
    }
}

void Champions36_ConfigureLeague(DIYCompetitionEx *competition)
{
    competition->matchData.league.totalMatchesPerSection = 144;
    competition->matchData.league.totalMatchesPerSectionLive = 144;
    competition->leagueTotalMatches = 8;
    competition->leagueSectionState.matchesPerHalf = 18;
    competition->minSubstitutes = 1;
    competition->pointsForAWin = 3;
}

/* Beyond each stage's working front, teamIndexOrder[0..35] is kept a FULL
 * PERMUTATION of the 36 clubs: eliminated clubs are parked behind the stage
 * front instead of being overwritten. Career mode searches the master after
 * elimination (FindTeamInDIY on the slot buffer, from the post-match
 * Career_FillCurrentSeasonInfo and the season-end chairman opinion, and the
 * load-time participation gate) and 0x6006-panics on a club it cannot find —
 * the stage loads only read order[0..stageTeams), so the tail is free. */
void Champions36_Advance(DIYTournamentState *tournament, DIYCompetitionEx *stage)
{
    uint8_t *order = tournament->teamIndexOrder;
    unsigned round = tournament->header.roundComplete;
    if (round == 0) {
        DIY_SortLeagueTableByStandings();
        const uint16_t *ranks = stage->matchData.league.teamEntryOffsets;
        /* Unused tail of the existing, serialized order buffer keeps the
         * top-eight byes while the play-off stage uses entries 0..15. */
        for (unsigned i = 0; i < 8; ++i) order[56 + i] = (uint8_t)(ranks[i] / 18);
        for (unsigned i = 0; i < 8; ++i) {
            order[2*i] = (uint8_t)(ranks[23-i] / 18); /* unseeded hosts leg 1 */
            order[2*i+1] = (uint8_t)(ranks[8+i] / 18);
        }
        /* Park ranks 25-36 (out) and the byes behind the play-off front. */
        for (unsigned i = 0; i < 12; ++i) order[16+i] = (uint8_t)(ranks[24+i] / 18);
        for (unsigned i = 0; i < 8; ++i)  order[28+i] = (uint8_t)(ranks[i] / 18);
    } else {
        /* The swap pass leaves the finished stage's grid compacted winners
         * [0..pairs), losers [pairs..2*pairs) — both halves master indexes. */
        DIY_ApplyHomeAwaySwapFlagsToTeamGridPairs();
        if (round == 1) {
            /* Fixed rank-based bracket: top two face the 15/16 v 17/18
             * paths; top 3/4 the 13/14 paths, etc. Opposite halves keep
             * ranks 1 and 2 apart until the final. */
            static const uint8_t seeded[8] = {0,7,3,4,1,6,2,5};
            for (unsigned i = 0; i < 8; ++i) {
                unsigned rank = seeded[i];
                order[2*i] = stage->teamGridPairings[7-rank];
                order[2*i+1] = order[56+rank];
            }
            /* Play-off losers replace the byes' copies at 28..35 (the byes
             * themselves just moved into the R16 front above). */
            for (unsigned i = 0; i < 8; ++i)
                order[28+i] = stage->teamGridPairings[8+i];
        } else {
            /* Winners AND losers: the losers stay parked right behind the
             * next stage's front, keeping the permutation. */
            memcpy(order, stage->teamGridPairings, stage->base.numTeams);
        }
    }
}

#ifdef SWOS_SDL_BUILD
/* Career mode searches the master for ELIMINATED clubs too (FindTeamInDIY on
 * the slot buffer) — every stage advance must leave order[0..35] a permutation
 * of the 36 clubs. */
static int orderIsPermutation(const uint8_t *order)
{
    uint8_t seen[36] = {0};
    for (unsigned i = 0; i < 36; ++i)
        if (order[i] >= 36 || seen[order[i]]++)
            return 0;
    return 1;
}

static int roundTripSelfTest(void)
{
    extern DIYCompetitionEx _diyCompetitionStart;
    extern DIYTournamentState _diyFileBufferCopy;
    extern uint16_t _numSelectedTeams;
    DIYCompetitionEx before = _diyCompetitionStart;
    DIYTournamentState tournament = _diyFileBufferCopy;
    char filename[80], path[1280];
    unsigned suffix = 0;
    /* Find an unused name; never replace a player's preset save. */
    for (;;) {
        snprintf(filename, sizeof filename, "CH36TEST-%lu-%u.PRE", (unsigned long)time(NULL), suffix++);
        snprintf(path, sizeof path, "%s/%s", swos_save_dir(), filename);
        FILE *existing = fopen(path, "rb");
        if (!existing) break;
        fclose(existing);
    }
    DIY_CopyDiyFileBufferCopyToCompetitionFileBuffer();
    int result = SaveJson_WritePreset((uint8_t *)filename);
    if (!result) result = SaveJson_LoadPreset((uint8_t *)filename, NULL, 0);
    if (!result) {
        DIY_CopyCompetitionFileBufferToDiyFileBufferCopy();
        result = _numSelectedTeams != 36 ||
            !Champions36_Is(&_diyFileBufferCopy.header) ||
            memcmp(tournament.teamIndexOrder, _diyFileBufferCopy.teamIndexOrder, 64) ||
            before.leagueSectionState.sectionMatchesProgress != _diyCompetitionStart.leagueSectionState.sectionMatchesProgress ||
            before.matchData.cup.currentMatchIndex != _diyCompetitionStart.matchData.cup.currentMatchIndex;
    }
    remove(path);
    if (result) fprintf(stderr, "Champions36 preset save/load failed\n");
    return result;
}

int Champions36_SelfTest(void)
{
    unsigned home[36] = {0}, away[36] = {0};
    uint8_t opponents[36][36] = {{0}};
    for (unsigned r = 0; r < 8; ++r) {
        unsigned played[36] = {0};
        for (unsigned p = 0; p < 18; ++p) {
            uint8_t a, b;
            Champions36_Fixture(r*18+p, &a, &b);
            if (a >= 36 || b >= 36 || a == b || played[a]++ || played[b]++ || opponents[a][b]) return 1;
            opponents[a][b] = opponents[b][a] = 1;
            home[a]++; away[b]++;
        }
        for (unsigned i = 0; i < 36; ++i) if (played[i] != 1) return 1;
    }
    for (unsigned i = 0; i < 36; ++i) if (home[i] != 4 || away[i] != 4) return 1;

    /* The three tier records substitute, carry their marker ids, and hold
     * 36 unique seeds each with no club in two competitions. */
    const Preset36 *tiers[3];
    tiers[0] = (const Preset36 *)Champions36_PresetRecord((uint8_t *)&_euroCup);
    tiers[1] = (const Preset36 *)Champions36_PresetRecord((uint8_t *)&_uefaCup);
    tiers[2] = (const Preset36 *)Champions36_PresetRecord((uint8_t *)&_cupWinnersCup);
    for (unsigned t = 0; t < 3; ++t) {
        if ((uint8_t *)tiers[t] == (uint8_t *)k_tiers[t].original) return 1;
        if (tiers[t]->header.comp.compId != COMPID_championsLeague + t) return 1;
        if (tiers[t]->header.comp.type != 2) return 1;
    }
    for (unsigned i = 0; i < 3*36; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (TEAMID_PACK(tiers[i/36]->teamSeedList[i%36]) ==
                TEAMID_PACK(tiers[j/36]->teamSeedList[j%36])) return 1;
    puts("Champions/Europa/Conference League: 144 fixtures, eight distinct opponents, 4 home/4 away, 3 x 36 unique seeds: PASS");

    /* The CAREER record: this season's qualified field heads the seeds
     * (deduped — the legacy UEFA fill can inject the player's club twice),
     * and the top-up never picks a club contesting another euro cup. */
    {
        uint8_t qual[32], othA[64], othB[32];
        for (unsigned i = 0; i < 15; ++i) {
            qual[2*i]   = (uint8_t)kEuropaSeeds[i].countryNumber;
            qual[2*i+1] = (uint8_t)kEuropaSeeds[i].teamOrdinal;
        }
        qual[30] = qual[0]; qual[31] = qual[1];   /* duplicate injection */
        for (unsigned i = 0; i < 32; ++i) {
            othA[2*i]   = (uint8_t)kConferenceSeeds[i].countryNumber;
            othA[2*i+1] = (uint8_t)kConferenceSeeds[i].teamOrdinal;
        }
        for (unsigned i = 0; i < 16; ++i) {
            othB[2*i]   = (uint8_t)kChampionsSeeds[i].countryNumber;
            othB[2*i+1] = (uint8_t)kChampionsSeeds[i].teamOrdinal;
        }
        const Preset36 *cr = (const Preset36 *)
            Champions36_CareerRecord(0, qual, 16, othA, 32, othB, 16);
        if (!cr || cr->header.comp.compId != COMPID_championsLeague) return 1;
        if ((uint8_t *)cr != Champions36_TierRecordIdentity(0)) return 1;
        for (unsigned i = 0; i < 15; ++i)
            if (TEAMID_PACK(cr->teamSeedList[i]) != TEAMID_PACK(kEuropaSeeds[i])) return 1;
        for (unsigned i = 0; i < 36; ++i)
            for (unsigned j = 0; j < i; ++j)
                if (TEAMID_PACK(cr->teamSeedList[i]) == TEAMID_PACK(cr->teamSeedList[j])) return 1;
        for (unsigned i = 15; i < 36; ++i) {
            uint16_t id = TEAMID_PACK(cr->teamSeedList[i]);
            for (unsigned j = 0; j < 32; ++j)
                if (id == BE_READ_U16(othA + 2*j)) return 1;
            for (unsigned j = 0; j < 16; ++j)
                if (id == BE_READ_U16(othB + 2*j)) return 1;
        }
        /* The career test rebuilt tier 0's record storage — restore the
         * curated preset seeds for the engine run below. */
        tiers[0] = (const Preset36 *)Champions36_PresetRecord((uint8_t *)&_euroCup);
        puts("Champions League: career record (qualified field + curated top-up, no double entry): PASS");
    }

    /* Exercise the real tournament loader, fixture pointers, league progress,
     * two-leg result processing and stage transitions without a renderer. */
    const Preset36 *preset = tiers[0];
    extern DIYCompetitionEx _diyCompetitionStart;
    extern DIYTournamentState _diyFileBufferCopy;
    extern uint16_t _numSelectedTeams, _gameType, _careerLoadedFlag;
    extern uint8_t *_fixtureTeam1CompPtr, *_fixtureTeam2CompPtr;
    extern int16_t _cachedViewResultTeam1Goals, _cachedViewResultTeam2Goals;
    extern int16_t _cachedExtraTimeTeam1Goals, _cachedExtraTimeTeam2Goals;
    extern int16_t _cachedPenaltyShootoutTeam1Score, _cachedPenaltyShootoutTeam2Score;
    extern void DIY_AdvanceToNextCompetitionSlot(void);
    _gameType = 2;
    _careerLoadedFlag = 1;
    _numSelectedTeams = 36;
    for (unsigned i = 0; i < 36; ++i) {
        TeamFileEx *team = &_selectedTeams[i];
        memset(team, 0, sizeof *team);
        team->team.countryNumber = preset->teamSeedList[i].countryNumber;
        team->team.teamOrdinal = preset->teamSeedList[i].teamOrdinal;
        team->team.teamControls = 1;
        snprintf((char *)team->team.teamName, sizeof team->team.teamName, "CLUB %02u", i);
        memcpy(&_diyFileBufferCopy.teamIds[i], &preset->teamSeedList[i], sizeof(TeamId));
    }
    InitDIYLeagueFromRecord((uint8_t *)preset);
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "Champions36 selftest line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
    DIYCompetitionEx *stage = &_diyCompetitionStart;
    DIYTournamentState *tournament = &_diyFileBufferCopy;
    CHECK(stage->base.numTeams == 36 && stage->leagueTotalMatches == 8);
    CHECK(tournament->header.totalMatchesInRound == 6);
    for (unsigned match = 0; match < 144; ++match) {
        CHECK(stage->minSubstitutes == 1);
        DIY_GetCurrentFixtureTeamPtrs();
        CHECK(_fixtureTeam1CompPtr != _fixtureTeam2CompPtr);
        _cachedViewResultTeam1Goals = 1;
        _cachedViewResultTeam2Goals = 0;
        CHECK(DIY_PlayOrSimulateLeagueFixture(0, 0) == 0);
        if (match == 71) CHECK(roundTripSelfTest() == 0);
    }
    CHECK(stage->minSubstitutes == 0 && stage->leagueMatchProgress == 8);
    CHECK(stage->matchesPlayedCount == 144);
    for (unsigned i = 0; i < 36; ++i) {
        CHECK(stage->leagueStandings[i].gamesPlayed == 8);
        CHECK(stage->leagueStandings[i].points == 12);
        /* Deterministic ranking, independent of the seed list's country IDs. */
        stage->leagueStandings[i].points = (uint16_t)(36-i);
    }
    DIY_AdvanceToNextCompetitionSlot();
    CHECK(tournament->header.roundComplete == 1);
    CHECK(orderIsPermutation(tournament->teamIndexOrder));
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(tournament->teamIndexOrder[56+i] == i);
        CHECK(stage->teamGridPairings[2*i] == 23-i);
        CHECK(stage->teamGridPairings[2*i+1] == 8+i);
    }
    for (unsigned i = 0; i < 12; ++i)
        CHECK(tournament->teamIndexOrder[16+i] == 24+i);
    unsigned knockoutMatches = 0;
    for (unsigned round = 1; round < 6; ++round) {
        unsigned teams = round <= 2 ? 16 : (32u >> (round-1));
        CHECK((unsigned)stage->base.numTeams == teams);
        CHECK(stage->base.playEachTeam == 0); /* no away goals */
        CHECK(stage->matchData.cup.roundLegs[0] == (round == 5 ? 0 : 2));
        unsigned played = 0;
        while (!stage->base.competitionFinished && played < 33) {
            _cachedViewResultTeam1Goals = 1;
            _cachedViewResultTeam2Goals = 0;
            _cachedExtraTimeTeam1Goals = 0;
            _cachedExtraTimeTeam2Goals = 0;
            _cachedPenaltyShootoutTeam1Score = 5;
            _cachedPenaltyShootoutTeam2Score = 4;
            CHECK(DIY_PlayCupMatchAndAdvance(0, 0) == 0);
            played++;
            if (round == 1 && played == 4) CHECK(roundTripSelfTest() == 0);
            if (stage->base.roundComplete && !stage->base.competitionFinished)
                DIY_UpdateCompetitionProgress();
        }
        CHECK(stage->base.competitionFinished);
        CHECK(played == (round == 5 ? 1 : teams));
        knockoutMatches += played;
        DIY_AdvanceToNextCompetitionSlot();
        CHECK(orderIsPermutation(tournament->teamIndexOrder));
        if (round == 1) {
            unsigned seen[36] = {0};
            for (unsigned i = 0; i < 16; ++i) {
                unsigned id = stage->teamGridPairings[i];
                CHECK(id < 24 && !seen[id]++);
            }
            for (unsigned i = 0; i < 8; ++i) CHECK(seen[i] == 1);
        }
    }
    CHECK(knockoutMatches == 45);
    CHECK(tournament->header.isActive == 2 && tournament->header.currentRoundIndex == 1);
    puts("Champions League: league progression, top-eight byes, play-offs, R16, QF, SF, final (189 matches): PASS");
    puts("Champions League: preset save/load during league and play-offs: PASS");
#undef CHECK
    return 0;
}
#endif
