/* custom_team.h -- MY CUSTOM TEAM: a team of the player's own for online play, made
 * from a team of the database with its names, faces, nationalities, shirt numbers, kits
 * and line-up changed -- and nothing else (src/custom_team.c, docs/CUSTOM_TEAMS.md). SDL only. */
#ifndef CUSTOM_TEAM_H
#define CUSTOM_TEAM_H

#include "game_types.h"

#include "kit_art.h"
#include "team_db.h"

#define CUSTOM_TEAM_SLOTS 3

/* A custom team as it is kept: a LINK to the database team it was made from, and what
 * its owner changed. It has no position, skill or value in it -- those are the
 * database's, found again through the ids (CustomTeam_Resolve). This is what a
 * CUSTOMn.TEAM file holds, as JSON. */
typedef struct CustomPlayer {
    char    uuid[TEAM_UUID_TEXT];       /* which of the database team's players he is */
    int8_t  name[23];
    uint8_t face;                       /* 0..2 */
    uint8_t nationality;                /* an index into the game's country names */
    int8_t  shirtNumber;
} CustomPlayer;

typedef struct CustomTeam {
    char     madeFrom[TEAM_UUID_TEXT];          /* the database team's id */
    char     checksum[TEAM_CHECKSUM_TEXT];      /* what that id stood for when this was made */
    int8_t   name[17];
    int8_t   coach[23];
    uint8_t  tactics;
    KitLook  kit[2];                            /* first, second */
    uint8_t  lineup[TEAM_PLAYERS];              /* TeamFile.playerNumbers */
    CustomPlayer players[TEAM_PLAYERS];
} CustomTeam;

/* A team as a CustomTeam: which database team it is -- by its number, else by its
 * players -- and what differs. 0 if its players are no database team's. */
int32_t CustomTeam_FromTeam(const TeamFileEx *team, CustomTeam *out);
/* The team a CustomTeam plays as: the database team its id names, which has to be
 * there and be the data its checksum says, with the changes on it. */
int32_t CustomTeam_Resolve(const CustomTeam *custom, TeamFileEx *out);

/* The team kept in a slot (0..2), built from the database and the slot's file.
 * 0 when the slot is empty or its file cannot be used. */
int32_t CustomTeam_Load(int32_t slot, TeamFileEx *out);
int32_t CustomTeam_Save(int32_t slot, const TeamFileEx *team);

/* Is this team a team of the database with only what a custom team may change
 * changed? Every player must be one of that team's players with his position, skills
 * and value as the database has them. For a team that arrives from
 * the other player: it touches no state of the game's (it reads the database into a
 * buffer of its own), so it is safe from the lobby's polling. */
int32_t CustomTeam_Allowed(const TeamFileEx *team);

/* Has this team been made someone's own -- a database team under another name, with
 * another coach, other kits, or players renamed, refaced or given another nationality?
 * A line-up, a formation or swapped shirt numbers alone do not make it one: every team
 * that has been through the team screen has those. For the team screen, which says
 * (CUSTOM) where it would name the country of the team's file. */
int32_t CustomTeam_IsCustom(const TeamFileEx *team);

/* What this team is, for the matchmaking server's team check (include/team_proto.h):
 * the id of the database team it is, and the checksum of its players as they are in
 * the record -- so a doctored team gives a checksum the server does not know. */
void CustomTeam_Claim(const TeamFileEx *team, uint8_t uuid[16], uint8_t checksum[8]);

/* MY CUSTOM TEAMS, from the matchmaking menu: the three slots; choosing one edits it
 * (an empty one first asks for a team of the database to start from). */
void CustomTeam_Manage(void);
/* For a fork's own MY CUSTOM TEAMS (its mod's overlay of swos2020/selectMyCustomTeams, whose
 * onDraw is the mod's and calls CustomTeam_SlotsDraw first -- SWOS United's IMPORT FROM SWOS
 * 2020): the slots screen's drawing, whether it is the pick for a match (not the editor), and
 * reading the slots again after the mod wrote one. */
void    CustomTeam_SlotsDraw(void);
int32_t CustomTeam_SlotsPicking(void);
void    CustomTeam_SlotsReload(void);

/* The same three slots as a way to pick a team for a match. 1 = a custom team was
 * picked and is in *out; 0 = the player asked for the ordinary team list instead;
 * -1 = there are no custom teams, so nothing was shown. */
int32_t CustomTeam_Pick(TeamFileEx *out);

/* While the EDIT TEAM screen is editing a custom team (edit_tactics.c asks): IMPORT
 * takes the whole team, identity included, and stays available; UNDO is LINEUP. */
int32_t CustomTeam_Editing(void);
void    CustomTeam_Lineup(void);
const char *CustomTeam_LineupLabel(void);

/* --convert-teams=<in>,<out>: every .TEAM record in <in> converted to <out>/<name>.TEAM (JSON inside). */
int32_t CustomTeam_ConvertDir(const char *in, const char *out);

/* --custom-team-selftest: every database team through the rule and through the file. */
int32_t CustomTeam_SelfTest(void);

#endif /* CUSTOM_TEAM_H */
