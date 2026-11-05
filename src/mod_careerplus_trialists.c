/*
 * mod_careerplus_trialists.c -- Career+'s trialists (CAREER_PLUS.md F8), at playerCreated: a
 * trialist the career club takes on (an emergency one, or a training one) is made a little worse
 * than the established players of his position, rather than the original's raw youngster:
 *
 *  - an outfield player: the seven ratings' total 5-10 below the rounded average of his
 *    position's players (any outfield player's when none plays it), shared out towards their
 *    profile with a little chance, each rating 0-7; his price that total;
 *  - a goalkeeper (no ratings): his price 5-10 below the goalkeepers' average.
 *
 * The fork's Career_GenerateSquadRelativeTrialistSkills (transfer_market.c at 8a6baf60), on the
 * mod's own random numbers. With no established player to measure by, the game's trialist stays.
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "game_types.h"

#ifdef SWOS_SDL_BUILD

/* A player's seven ratings in the file's nibble order: passing, velocity, heading, tackling,
 * control, speed, finishing (0-7 each). */
static void ratings_of(const PlayerFile *p, uint8_t a[7])
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

static uint8_t position_of(const PlayerFile *p)
{
    return (uint8_t)((p->positionAndFace & PF_POSITION_MASK) >> 5);
}

void CP_Trialists_Created(const ModPlayerCreated *created)
{
    PlayerFile squad[16];
    PlayerFile *pf = created->player;
    if (created->kind != MODPLAYER_TRIALIST || ModApi_CareerTeam(created->team, squad) != 0)
        return;
    const uint8_t position = position_of(pf);
    uint32_t sums[7] = { 0 }, count = 0, priceSum = 0;
    for (int32_t pass = 0; pass < 2 && count == 0; pass++) {
        for (int32_t i = 0; i < 16; i++) {
            const PlayerFile *o = &squad[i];
            const uint8_t form = (uint8_t)o->form, at = position_of(o);
            if (i == created->slot || form == PLAYER_FORM_TRIALIST || form == PLAYER_FORM_RESERVE ||
                !o->playerName[0] || (uint8_t)o->playerName[0] == 0xFF || (uint8_t)o->price > 49)
                continue;
            if (position == 0 ? at != 0 : pass == 0 ? at != position : at == 0)
                continue;
            uint8_t a[7];
            ratings_of(o, a);
            for (int32_t k = 0; k < 7; k++)
                sums[k] += a[k];
            priceSum += (uint8_t)o->price;
            count++;
        }
    }
    if (!count)
        return;
    if (position == 0) {
        int32_t price = (int32_t)((priceSum + count / 2) / count) - 5 - CP_RandBelow(6);
        price = price < 0 ? 0 : price > 49 ? 49 : price;
        pf->passing = pf->velocityHeading = pf->tacklingControl = pf->speedFinishing = 0;
        pf->price = (int8_t)price;
        return;
    }
    uint32_t peerTotal = 0;
    for (int32_t k = 0; k < 7; k++)
        peerTotal += sums[k];
    int32_t target = (int32_t)((peerTotal + count / 2) / count) - 5 - CP_RandBelow(6);
    target = target < 0 ? 0 : target > 49 ? 49 : target;
    uint8_t a[7] = { 0 };
    for (int32_t point = 0; point < target; point++) {     /* (towards the squad's profile) */
        int32_t best = -1, bestScore = -32767;
        for (int32_t k = 0; k < 7; k++) {
            if (a[k] >= 7)
                continue;
            const int32_t score = (int32_t)(sums[k] * 8 / count) - a[k] * 8 + CP_RandBelow(17);
            if (score > bestScore)
                bestScore = score, best = k;
        }
        if (best < 0)
            break;
        a[best]++;
    }
    pf->passing = (int8_t)a[0];
    pf->velocityHeading = (int8_t)((a[1] << 4) | a[2]);
    pf->tacklingControl = (int8_t)((a[3] << 4) | a[4]);
    pf->speedFinishing = (int8_t)((a[5] << 4) | a[6]);
    pf->price = (int8_t)target;
}

#endif /* SWOS_SDL_BUILD */
