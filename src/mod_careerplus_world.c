/*
 * mod_careerplus_world.c -- the career's clubs in memory, for the features that work on all of
 * them at once (the AI transfers, the retirements, the searches): read with ModApi_CareerClubs,
 * changed here, and each changed player written back through the game's own career code
 * (ModApi_SetCareerPlayer, ModApi_SetOwnPlayer for the career club's).
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "game_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

static CP_Club *s_clubs;
static int32_t s_count, s_cap;

static int32_t collect(const ModCareerClub *club, void *ctx)
{
    (void)ctx;
    if (s_count == s_cap) {
        const int32_t cap = s_cap ? s_cap * 2 : 2048;
        CP_Club *p = (CP_Club *)realloc(s_clubs, (size_t)cap * sizeof *p);
        if (!p)
            return 1;
        s_clubs = p;
        s_cap = cap;
    }
    CP_Club *c = &s_clubs[s_count++];
    c->team = club->team;
    c->league = club->league;
    memcpy(c->name, club->name, sizeof c->name);
    memcpy(c->order, club->order, sizeof c->order);
    memcpy(c->players, club->players, sizeof c->players);
    return 0;
}

int32_t CP_World_Read(void)
{
    s_count = 0;
    if (ModApi_CareerClubs(collect, NULL) < 0) {
        fprintf(stderr, "[careerplus] the clubs could not be read\n");
        s_count = 0;
        return -1;
    }
    return s_count;
}

int32_t CP_World_Count(void) { return s_count; }
CP_Club *CP_World_Club(int32_t i) { return i >= 0 && i < s_count ? &s_clubs[i] : NULL; }

CP_Club *CP_World_Find(uint16_t team)
{
    for (int32_t i = 0; i < s_count; i++)
        if (s_clubs[i].team == team)
            return &s_clubs[i];
    return NULL;
}

int32_t CP_World_Write(const CP_Club *club, int32_t slot)
{
    extern uint16_t _selTeamNumber;
    if (club->team == _selTeamNumber)
        return ModApi_SetOwnPlayer((uint16_t)slot, &club->players[slot]);
    return ModApi_SetCareerPlayer(club->team, (uint16_t)slot, &club->players[slot]);
}

/* Every integer of a JSON slice of numbers ([[1,2],[3,-4]] -> 1 2 3 -4), in order, in one pass:
 * the mod's saved arrays are numbers only, and mjson_next walks an array from its start at each
 * element (20,000 ages took 9 s). How many were read, at most `max`. */
int32_t CP_Numbers(const char *s, int32_t len, int32_t *out, int32_t max)
{
    int32_t n = 0;
    for (int32_t i = 0; i < len && n < max; i++) {
        if (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')) {
            const int32_t neg = s[i] == '-';
            int32_t v = 0;
            if (neg)
                i++;
            while (i < len && s[i] >= '0' && s[i] <= '9')
                v = v * 10 + (s[i++] - '0');
            out[n++] = neg ? -v : v;
        }
    }
    return n;
}

int32_t CP_PlayerValid(const PlayerFile *p)
{
    return p->playerName[0] && (uint8_t)p->playerName[0] != 0xFF && ModApi_PlayerValue(p) > 0;
}

uint8_t CP_Position(const PlayerFile *p)
{
    return (uint8_t)((p->positionAndFace & PF_POSITION_MASK) >> 5);
}

/* The seven ratings in the file's nibble order -- passing, velocity, heading, tackling, control,
 * speed, finishing -- 0-7 each (a set top bit read as 7, as the fork reads its peers). */
void CP_Ratings(const PlayerFile *p, uint8_t a[7])
{
    const uint8_t packed[4] = { (uint8_t)p->passing, (uint8_t)p->velocityHeading,
                                (uint8_t)p->tacklingControl, (uint8_t)p->speedFinishing };
    a[0] = packed[0] & 15;
    a[1] = packed[1] >> 4, a[2] = packed[1] & 15;
    a[3] = packed[2] >> 4, a[4] = packed[2] & 15;
    a[5] = packed[3] >> 4, a[6] = packed[3] & 15;
    for (int32_t k = 0; k < 7; k++)
        if (a[k] > 7)
            a[k] = 7;
}

void CP_SetRatings(PlayerFile *p, const uint8_t a[7])
{
    p->passing = (int8_t)a[0];
    p->velocityHeading = (int8_t)((a[1] << 4) | a[2]);
    p->tacklingControl = (int8_t)((a[3] << 4) | a[4]);
    p->speedFinishing = (int8_t)((a[5] << 4) | a[6]);
}

#endif /* SWOS_SDL_BUILD */
