/*
 * world_playoff.h -- the career club's own play-off, played (src/world_playoff.c;
 * docs/WORLD_2026_27.md, phase 4).
 *
 * A world's play-offs and play-outs are decided at the season end (league_playoffs.h).
 * When the career club takes part in one, it is a contest it plays instead: built when the
 * club's league has played its last match day, into competition slot 4 -- where 96/97's
 * English play-offs are -- dated after the league; its result then stands at the season
 * end in place of the simulated one (as the SWOS 96/97 Mod's dp_lend / dp_end do).
 *
 * Serie B's play-out (the 17th v the 16th, two legs; level: the 16th stays, or extra time
 * and penalties when they finished level on points) and play-off (the 3rd-8th: 5th v 8th and
 * 6th v 7th single, extra time, at the better placed's; the 3rd and the 4th wait -- byes; semis
 * 3rd v W(6v7), 4th v W(5v8), two legs, the return at the 3rd's / 4th's; a final of two legs,
 * the first at the worse placed's, extra time and penalties when the two are level on points;
 * any other level tie: the better placed).
 *
 * And a group's play-out (the pack's knockouts, gplay.c: Serie C's 16th v 19th, 17th v 18th,
 * two legs; Serie D's single, extra time): its ties are independent, so the career club's
 * bracket is its own tie, a contest of two (the other tie is decided at the season end).
 *
 * And a Serie D group's placement play-off (the pack's knockouts: 2nd v 5th, 3rd v 4th,
 * then the winners; single, extra time, then the better placed): it moves no club at the
 * season end (gplay.c leaves it to the repechage), so it is played for itself.
 *
 * And Serie C's national play-off (dplay.c c_lend / dp_perm_c): 28 clubs, the groups' 2nd-10th
 * and the cup winner's place -- group rounds (5th v 10th, 6th v 9th, 7th v 8th; then the 3
 * winners and the 4th, best v worst), national rounds with seeded draws (the 3rds, the cup
 * place, the best qualifier; then the 2nds and the best winner), a Final Four. It needs the
 * other two groups' tables when the club's group ends: they are simulated then, and the
 * season end gets their results back.
 *
 * A bye is a club's tie with itself, listed after the round's real ties and not played
 * (the round plays totalMatchesInRound of its matchPairsPerRound ties): the cup engine's
 * halving bracket holds -- 8 places for Serie B's 6 clubs.
 */
#ifndef WORLD_PLAYOFF_H
#define WORLD_PLAYOFF_H

#include "swos_types.h"

enum { WORLD_PLAYOFF_NONE, WORLD_PLAYOFF_B_PLAYOUT, WORLD_PLAYOFF_B_PLAYOFF, WORLD_PLAYOFF_GROUP_TIE,
       WORLD_PLAYOFF_C_PLAYOFF, WORLD_PLAYOFF_PLACEMENT, WORLD_PLAYOFF_SPEC, WORLD_PLAYOFF_GROUPS };

/* The contest under way or played this season (saved with the career). */
#define WORLD_PLAYOFF_MAX 32
#define WORLD_PLAYOFF_EARLY_ROWS 24
#define WORLD_PLAYOFF_EARLY 9           /* (a group contest's other divisions: the Mod's SDX) */

/* A group's final table simulated early, kept for the season end (dplay.c dp_restore). */
typedef struct {
    uint16_t team, played, won, drawn, lost, gf, ga, pts;
} WorldPlayoffRow;

typedef struct {
    uint8_t  kind;              /* WORLD_PLAYOFF_*; NONE: no contest */
    uint8_t  n;                 /* its clubs */
    uint8_t  etp;               /* level decided by penalties, not by the better placed */
    uint8_t  rules;             /* a group's tie: its round's flag byte (legs / e.t. / pens) */
    uint8_t  rules2;            /* a group's placement play-off: its final's */
    uint16_t team[WORLD_PLAYOFF_MAX];   /* {country, ordinal} as the game packs it, entrant order */
    uint8_t  prio[WORLD_PLAYOFF_MAX];   /* a level tie goes to the lower (the place, the seed) */
    uint16_t pts[WORLD_PLAYOFF_MAX];    /* their points, wins, goals scored, places, groups */
    uint16_t won[WORLD_PLAYOFF_MAX];
    uint16_t gf[WORLD_PLAYOFF_MAX];
    uint8_t  place[WORLD_PLAYOFF_MAX];
    uint8_t  group[WORLD_PLAYOFF_MAX];
    uint8_t  cup;               /* the Coppa Italia Serie C winner's entrant (its stand-in) */
    uint8_t  ties;              /* the round under way: its real ties (entrants), the byes after */
    uint8_t  tieA[16], tieB[16];
    uint8_t  early;             /* the other groups simulated early: their divisions, tables */
    uint8_t  earlyDiv[WORLD_PLAYOFF_EARLY], earlyN[WORLD_PLAYOFF_EARLY];
    WorldPlayoffRow earlyRow[WORLD_PLAYOFF_EARLY][WORLD_PLAYOFF_EARLY_ROWS];
    uint16_t winner;            /* its last tie's winner, 0: not decided yet */
    /* WORLD_PLAYOFF_SPEC: a play-off of the pack's spec, played (the Mod's gp_ko knockouts) */
    uint8_t  country, pidx;     /* the spec: the country's, its play-off index */
    uint8_t  stage;             /* a stage title's stage (0..2), 0xFF: a season-end play-off */
    uint8_t  season;            /* (g_worldCups.seasons + 1 when built) */
    uint8_t  week;              /* its first week (mid-season: after its stage's last); 0: after the league */
    uint8_t  tw[WORLD_PLAYOFF_MAX], tl[WORLD_PLAYOFF_MAX];   /* each tie's winner / loser (entrant), 0xFF */
    uint8_t  tieT[16];          /* the round under way: its real ties' tie indices */
    uint16_t glob[WORLD_PLAYOFF_MAX];   /* the entrants' global numbers */
    /* its game rounds (the Mod's gp_ko: a best-of-three round is three single-match rounds):
     * the contest's first (the rounds before decided at its build), the game rounds skipped
     * since (a game 3 nobody needed); a series' wins by tie, the better placed's in the low
     * nibble */
    uint8_t  g0, skip;
    uint8_t  gend, more;        /* a contest of more than the engine's six rounds is played in parts
                                 * (the MLS's wild card club: seven): this part's end game round (0:
                                 * the last), the next part to build at the calendar's advance */
    uint8_t  sw[WORLD_PLAYOFF_MAX];
} WorldPlayoffState;

/* A stage title the career club's division decided mid-season (its knockout played or
 * decided at once), read by the season end instead of a simulation (the Mod's GP_ST.dec). */
typedef struct {
    uint8_t  season, done;      /* (g_worldCups.seasons + 1) */
    uint16_t team[2], glob[2];  /* the champion, the runner-up (0xFFFF none) */
} WorldStageTitle;
extern WorldStageTitle g_worldStageTitle[3];

/* A split stage of the career club's league just ended (league_split.c) whose end builds a
 * stage-title knockout (the split's 'then', the pack's build_after): play-off pidx; it is
 * built when the club plays its first round, else decided at the season end. */
void WorldPlayoff_StageEnded(uint8_t country, uint16_t division, int32_t pidx, uint8_t week);
void WorldPlayoff_Advance(uint16_t week);  /* (the calendar's advance: a deferred build) */
/* The season end (league_playoffs.c): a played play-off's ties (team numbers), for pidx. */
int32_t WorldPlayoff_PlayedTie(int32_t pidx, uint16_t a, uint16_t b, uint16_t *winner);
/* ... and a played group contest's (WORLD_PLAYOFF_GROUPS) champion and runner-up, for pidx. */
int32_t WorldPlayoff_PlayedTitle(int32_t pidx, uint16_t *champion, uint16_t *runnerUp);
const WorldStageTitle *WorldPlayoff_StageTitle(int32_t stage);
void WorldPlayoff_ResetStageTitles(void);   /* (a new career, a save without them) */

extern WorldPlayoffState g_worldPlayoff;

/* A new season / career: no contest. */
void WorldPlayoff_Reset(void);

/* The career club's league has played its last match day: build its play-off, if it has one.
 * 1 when slot 4 holds a world's play-off (built now or before): 96/97's slot-4 rebuild
 * (DIY_RebuildCompetitionAndTeamSelectionCaches, the English play-offs) is not for it. */
int32_t WorldPlayoff_CareerLeagueEnded(void);

/* A tie of the contest under way ended level, and its rules give it no replay: 1 if team1
 * goes through, 2 if team2, 0 if not ours (the game's rule stands). */
int32_t WorldPlayoff_LevelWinner(uint16_t team1, uint16_t team2);

/* A tie of the contest under way was decided. */
void WorldPlayoff_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won);

/* A round of the contest under way has been played and its winners gathered
 * (DIY_UpdateCompetitionProgress): the next round's ties and rules. */
void WorldPlayoff_ArrangeNextRound(void);

/* After the engine halves the bracket for the next round: only that round's real ties
 * are played (its byes after them). */
void WorldPlayoff_RoundSize(void);

/* The season end has simulated division d afresh: a group simulated early gets the early
 * results, place by place (dplay.c dp_restore), so the play-off's seeding stands. */
void WorldPlayoff_RestoreEarlyTable(uint16_t division);

/* A game round's name of the contest (the pack's 'round_names': _cupRoundStringTable's index),
 * -1: not ours, or the game's own. */
int32_t WorldPlayoff_RoundStringIndex(uint8_t compId, uint16_t round);

/* A round's name for the contest under way where the engine's would mislead, or 0. */
const char *WorldPlayoff_RoundName(uint16_t round, uint16_t rounds);

/* A loaded career's group contest in slot 3 (_euroCupSlotIndex 9): its record, 0 for none. */
uint8_t *WorldPlayoff_GroupsRecord(void);

/* After a career is loaded: slot 4 back on the contest, if one is under way. */
void WorldPlayoff_RestoreSlot(void);

#endif
