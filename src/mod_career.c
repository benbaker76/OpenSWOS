/*
 * mod_career.c -- what a career mod may ask the game about, and do to, the career
 * (include/mod_api.h "the career"; docs/MODS.md §31; benefactorr's Career+, CAREER_PLUS.md §3.5).
 * Both targets. Each goes through the game's own career code, never around it.
 */
#include "mod_api.h"

#include "game_types.h"
#include "career_params.h"

#include <stdio.h>

extern int32_t Career_PeekTeam(uint16_t teamId, TeamFileEx *out);                       /* career_teams.c */
extern int32_t Career_WriteTeamPlayer(uint16_t teamId, uint16_t slot, const PlayerFile *player);   /* transfer_market.c */
extern void    ShowPlayerOfferMenuForPlayer(uint16_t teamNumber, uint16_t playerOrdinal);
extern uint16_t GetRemainingTransferPoolSlots(void);
extern int16_t FindPlayerOnTransferMarket(uint16_t playerId);
extern void    AddPlayerToTransferMarket(uint16_t playerId);
extern uint8_t *GetPlayer(uint16_t teamNumber, uint16_t playerIndex);
extern uint16_t _gameType, _isNationalSelector, _selTeamNumber, _timeToNegotiate, _transferCompleted;
extern uint16_t _numPlayersOnTransferMarket, _careerNumPlayers;
extern uint8_t  _nationalTeamNationality;
extern uint16_t _careerUnavailablePlayerList[], _careerUnavailablePlayerLookupTable[];

enum { kGameTypeCareer = 4 };

static int32_t in_career(void) { return _gameType == kGameTypeCareer; }

int32_t ModApi_CareerTeam(uint16_t team, struct PlayerFile *out)
{
    TeamFileEx t;
    if (!in_career() || !out || !Career_PeekTeam(team, &t))
        return -1;
    for (int32_t i = 0; i < 16; i++)
        out[i] = t.players[i];
    return 0;
}

int32_t ModApi_CareerTeamName(uint16_t team, char *out, int32_t cap)
{
    TeamFileEx t;
    if (!in_career() || !out || cap < 1 || !Career_PeekTeam(team, &t))
        return -1;
    int32_t n = 0;
    while (n < cap - 1 && n < (int32_t)sizeof t.team.teamName && t.team.teamName[n])
        out[n] = (char)t.team.teamName[n], n++;     /* (17 letters have no NUL) */
    out[n] = 0;
    return 0;
}

int32_t ModApi_ViewedCompetition(uint8_t *competitionId, int32_t *league, uint8_t *division)
{
    extern int32_t WorldWinners_Viewed(uint8_t *competitionId, int32_t *league, uint8_t *division);
    if (!in_career() || !competitionId || !league || !division)
        return -1;
    return WorldWinners_Viewed(competitionId, league, division);
}

/* The teams found so far (ModApi_SetCareerPlayer's check, and every club ModApi_CareerClubs
 * visits): a team is there for good once found, and finding one reads its team file -- for each
 * of a batch's thousands of writes, minutes (Career+'s AI transfers). */
static uint8_t s_teamKnown[65536 / 8];

static int32_t team_exists(uint16_t team)
{
    TeamFileEx t;
    if (s_teamKnown[team >> 3] >> (team & 7) & 1)
        return 1;
    if (!Career_PeekTeam(team, &t))
        return 0;
    s_teamKnown[team >> 3] |= (uint8_t)(1u << (team & 7));
    return 1;
}

typedef struct { int32_t (*each)(const ModCareerClub *club, void *ctx); void *ctx; } ClubWalk;

static int32_t club_visited(const TeamFileEx *t, void *ctx)
{
    const ClubWalk *w = (const ClubWalk *)ctx;
    ModCareerClub club;
    club.team = PACK_BE_U16(t->team.countryNumber, t->team.teamOrdinal);
    s_teamKnown[club.team >> 3] |= (uint8_t)(1u << (club.team & 7));
    club.league = t->team.league;
    int32_t n = 0;
    while (n < 16 && t->team.teamName[n])
        club.name[n] = (char)t->team.teamName[n], n++;
    club.name[n] = 0;
    for (int32_t i = 0; i < 16; i++)
        club.order[i] = t->team.playerNumbers[i];
    club.players = t->players;
    return w->each(&club, w->ctx);
}

int32_t ModApi_CareerClubs(int32_t (*each)(const ModCareerClub *club, void *ctx), void *ctx)
{
    extern int32_t Career_ForEachClub(int32_t (*each)(const TeamFileEx *club, void *ctx), void *ctx);
    ClubWalk w = { each, ctx };
    if (!in_career() || !each)
        return -1;
    return Career_ForEachClub(club_visited, &w);
}

int32_t ModApi_PlayerValue(const struct PlayerFile *player)
{
    extern uint16_t _avgTeamValueBases[];   /* (92: the value in thousands by band) */
    extern int8_t _avgTeamValues[];         /* (the band by price index) */
    if (!player || !player->playerName[0] || (uint8_t)player->playerName[0] == 0xFF ||
        (uint8_t)player->price > 49)
        return -1;
    const int32_t band = (uint8_t)_avgTeamValues[(uint8_t)player->price];
    return band >= 1 && band <= 91 ? _avgTeamValueBases[band] : -1;
}

int32_t ModApi_SetOwnPlayer(uint16_t slot, const struct PlayerFile *player)
{
    extern int32_t Career_WriteOwnPlayer(uint16_t slot, const PlayerFile *player);
    if (!in_career())
        return -1;
    return Career_WriteOwnPlayer(slot, player);
}

int32_t ModApi_SetCareerPlayer(uint16_t team, uint16_t slot, const struct PlayerFile *player)
{
    if (!in_career() || !team_exists(team))
        return -1;
    return Career_WriteTeamPlayer(team, slot, player);
}

int32_t ModApi_OfferForPlayer(uint16_t team, uint16_t slot)
{
    TeamFileEx t;
    if (!in_career() || _isNationalSelector || team == _selTeamNumber || slot >= 16 || !Career_PeekTeam(team, &t) ||
        (uint8_t)t.players[slot].playerName[0] == 0xFF)
        return -1;
    if (_timeToNegotiate == 0 || GetRemainingTransferPoolSlots() < 2)     /* (BuyPlayerMenu's tests) */
        return -1;
    _transferCompleted = 0;
    ShowPlayerOfferMenuForPlayer(team, slot);
    return _transferCompleted == 1;
}

/* the pick ModApi_CallUpPlayer keeps for ADD PLAYER (menus.c NationalTeamAddPlayerModRow) */
static int32_t  s_callUp;
static uint16_t s_callUpTeam, s_callUpSlot;

int32_t ModApi_CallUpPlayer(uint16_t team, uint16_t slot)
{
    TeamFileEx t;
    if (!in_career() || !_isNationalSelector || slot >= 16 || !Career_PeekTeam(team, &t) ||
        (uint8_t)t.players[slot].playerName[0] == 0xFF || t.players[slot].nationality != (int8_t)_nationalTeamNationality)
        return -1;
    /* (the squad screen's greyed-out players: called up elsewhere, or unavailable -- as
     * Career_AddInternationalPlayerCommon's masks) */
    for (int32_t i = 0; i < 20; i++)
        if (_careerUnavailablePlayerLookupTable[2 * i] == team && _careerUnavailablePlayerLookupTable[2 * i + 1] == slot)
            return -1;
    for (int32_t i = 0; i < 4; i++)
        if (_careerUnavailablePlayerList[2 * i] == t.team.globalTeamNumber && _careerUnavailablePlayerList[2 * i + 1] == slot)
            return -1;
    s_callUp = 1;
    s_callUpTeam = team;
    s_callUpSlot = slot;
    return 0;
}

int32_t ModCareer_CallUpPending(void) { return s_callUp; }

int32_t ModCareer_TakeCallUp(uint16_t *team, uint16_t *slot)
{
    if (!s_callUp)
        return 0;
    s_callUp = 0;
    *team = s_callUpTeam;
    *slot = s_callUpSlot;
    return 1;
}

int32_t ModApi_ListPlayer(uint16_t slot)
{
    if (!in_career() || _isNationalSelector || slot >= 32)
        return -1;
    const PlayerFile *pf = (const PlayerFile *)(GetPlayer(_selTeamNumber, slot) + sizeof(TeamFile));
    if ((uint8_t)pf->playerName[0] == 0xFF)
        return -1;
    if (FindPlayerOnTransferMarket(slot) == 0)
        return 1;                   /* (listed already: the game's own add does not check) */
    if (_numPlayersOnTransferMarket >= _careerParams.transfer_list_capacity)
        return -1;
    AddPlayerToTransferMarket(slot);
    return 0;
}
