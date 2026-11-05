/*
 * mod_careerplus_ai.c -- Career+'s AI transfers (CAREER_PLUS.md F1): once a season, and again
 * whenever the manager asks (AI TRANSFERS, on the transfer market), the computer's clubs swap
 * players -- like for like: the same position, worth within 30% of each other, mostly for a
 * better one -- then each puts its best men in its starting eleven (the line-up tidy).
 *
 * A batch: every club (not yours) makes 4-6 exchanges, an exchange counting for both clubs.
 * In turn, in a shuffled order, each club tries up to 1100 times while under its quota: 15% for
 * a foreign partner (any club, wasted when of its own country), else one of its own country;
 * 80% wanting a better player. It picks one of its slots, then the partner's first fitting
 * player from a random slot. Neither player may have moved in this batch or the two before.
 * No money moves; the two players change places, ages and all. The tidy: up to five times a
 * club, the bench player who most outvalues a starter of his position (not a trialist or a
 * reserve) changes places with him.
 *
 * The fork's SWOS_AI_RealTransferTest and SWOS_AI_P3OptimiseLineup (career_init.c at 8a6baf60),
 * on the mod's own random numbers (his drew on the clock: no two runs alike).
 */
#include "mod_api.h"
#include "mod_careerplus.h"
#include "game_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef SWOS_SDL_BUILD

static uint16_t s_batch;        /* batches made in this career (the cooldown counts by them) */

uint16_t CP_AI_Batches(void) { return s_batch; }
void CP_AI_SetBatches(uint16_t n) { s_batch = n; }

static int32_t on_cooldown(uint16_t team, int32_t slot)
{
    const uint16_t moved = CP_MovedBatch(team, slot);
    return moved && s_batch - moved <= 2;
}

static int32_t swap_candidate(const PlayerFile *a, const PlayerFile *b, int32_t wantHigher)
{
    if (CP_Position(a) != CP_Position(b) || !CP_PlayerValid(a) || !CP_PlayerValid(b))
        return 0;
    const int32_t va = ModApi_PlayerValue(a), vb = ModApi_PlayerValue(b);
    if (va * 70 > vb * 100 || vb * 100 > va * 130)
        return 0;
    return wantHigher ? vb > va : vb < va;
}

static void swap_players(CP_Club *a, int32_t sa, CP_Club *b, int32_t sb)
{
    const PlayerFile t = a->players[sa];
    a->players[sa] = b->players[sb];
    b->players[sb] = t;
    CP_SwapAges(a->team, sa, b->team, sb);
}

/* The line-up tidy of one club: writes the slots it swaps. */
static int32_t tidy(CP_Club *club)
{
    uint8_t seen[16] = { 0 };
    for (int32_t i = 0; i < 16; i++) {
        if (club->order[i] >= 16 || seen[club->order[i]])
            return 0;               /* (not a line-up: leave it) */
        seen[club->order[i]] = 1;
    }
    int32_t swaps = 0;
    for (; swaps < 5; swaps++) {
        int32_t bestGain = 0, bs = -1, bb = -1;
        for (int32_t i = 0; i < 11; i++) {
            const PlayerFile *s = &club->players[club->order[i]];
            if (!CP_PlayerValid(s))
                continue;
            const int32_t vs = ModApi_PlayerValue(s);
            for (int32_t j = 11; j < 16; j++) {
                const PlayerFile *b = &club->players[club->order[j]];
                const uint8_t form = (uint8_t)b->form;
                if (!CP_PlayerValid(b) || CP_Position(b) != CP_Position(s) ||
                    form == PLAYER_FORM_TRIALIST || form == PLAYER_FORM_RESERVE)
                    continue;
                const int32_t gain = ModApi_PlayerValue(b) - vs;
                if (gain > bestGain)
                    bestGain = gain, bs = club->order[i], bb = club->order[j];
            }
        }
        if (bs < 0)
            break;
        swap_players(club, bs, club, bb);
        if (CP_World_Write(club, bs) != 0 || CP_World_Write(club, bb) != 0)
            return -1;
    }
    return swaps;
}

/* One batch over the clubs CP_World read: its exchanges, then every club's tidy. */
int32_t CP_AI_Batch(void)
{
    extern uint16_t _selTeamNumber;
    const int32_t n = CP_World_Count();
    if (n < 2)
        return 0;
    s_batch++;
    int32_t *order = (int32_t *)malloc((size_t)n * sizeof *order);
    uint8_t *quota = (uint8_t *)malloc((size_t)n), *made = (uint8_t *)calloc((size_t)n, 1);
    uint16_t *used = (uint16_t *)calloc((size_t)n, sizeof *used);     /* slots used this batch */
    int32_t *byFile = (int32_t *)malloc(256 * sizeof *byFile), *fileCount = (int32_t *)calloc(256, sizeof *fileCount);
    int32_t *ofFile = (int32_t *)malloc((size_t)n * sizeof *ofFile);
    int32_t exchanges = 0, full = 0;
    if (!order || !quota || !made || !used || !byFile || !fileCount || !ofFile)
        goto done;
    /* the clubs by country (a file's clubs together: CP_World reads them so) */
    for (int32_t f = 0; f < 256; f++)
        byFile[f] = -1;
    for (int32_t i = 0; i < n; i++) {
        const int32_t f = CP_World_Club(i)->team >> 8;
        if (byFile[f] < 0)
            byFile[f] = i;
        ofFile[i] = f;
        fileCount[f]++;
        order[i] = i;
        quota[i] = (uint8_t)(4 + CP_RandBelow(3));
    }
    for (int32_t i = n - 1; i > 0; i--) {
        const int32_t j = CP_RandBelow(i + 1), t = order[i];
        order[i] = order[j], order[j] = t;
    }
    for (int32_t k = 0; k < n && !full; k++) {
        const int32_t ia = order[k];
        CP_Club *a = CP_World_Club(ia);
        if (a->team == _selTeamNumber)
            continue;
        for (int32_t attempt = 0; attempt < 1100 && made[ia] < quota[ia] && !full; attempt++) {
            const int32_t foreign = CP_RandBelow(100) < 15, wantHigher = CP_RandBelow(100) < 80;
            const int32_t sa = CP_RandBelow(16);
            if ((used[ia] >> sa & 1) || !CP_PlayerValid(&a->players[sa]) || on_cooldown(a->team, sa))
                continue;
            int32_t ib;
            if (foreign) {
                ib = CP_RandBelow(n);
                if (ofFile[ib] == ofFile[ia])
                    continue;
            } else {
                const int32_t f = ofFile[ia];
                if (fileCount[f] < 2)
                    continue;
                ib = byFile[f] + CP_RandBelow(fileCount[f]);
            }
            CP_Club *b = CP_World_Club(ib);
            if (ib == ia || b->team == _selTeamNumber || made[ib] >= quota[ib])
                continue;
            const int32_t start = CP_RandBelow(16);
            int32_t sb = -1;
            for (int32_t d = 0; d < 16 && sb < 0; d++) {
                const int32_t s = (start + d) & 15;
                if (!(used[ib] >> s & 1) && !on_cooldown(b->team, s) &&
                    swap_candidate(&a->players[sa], &b->players[s], wantHigher))
                    sb = s;
            }
            if (sb < 0)
                continue;
            swap_players(a, sa, b, sb);
            if (CP_World_Write(a, sa) != 0 || CP_World_Write(b, sb) != 0) {
                fprintf(stderr, "[careerplus] the career's pool is full: the AI transfers stop\n");
                full = 1;
                break;
            }
            used[ia] |= (uint16_t)(1u << sa);
            used[ib] |= (uint16_t)(1u << sb);
            CP_SetMovedBatch(a->team, sa, s_batch);
            CP_SetMovedBatch(b->team, sb, s_batch);
            made[ia]++, made[ib]++;
            exchanges++;
        }
    }
    int32_t tidied = 0;
    for (int32_t i = 0; i < n && !full; i++) {
        CP_Club *c = CP_World_Club(i);
        if (c->team == _selTeamNumber)
            continue;
        const int32_t t = tidy(c);
        if (t < 0)
            full = 1;
        else
            tidied += t;
    }
    fprintf(stderr, "[careerplus] AI transfers, batch %u: %d exchanges, %d line-up changes\n",
            (unsigned)s_batch, (int)exchanges, (int)tidied);
done:
    free(order), free(quota), free(made), free(used), free(byFile), free(fileCount), free(ofFile);
    return exchanges;
}

/* AI TRANSFERS (the transfer market's mod row): one batch more, now. */
static void ai_transfers_now(void)
{
    static char text[48];
    if (CP_World_Read() < 0)
        return;
    const int32_t n = CP_AI_Batch();
    CP_Search_Fresh();
    snprintf(text, sizeof text, "THE CLUBS MADE %d TRANSFERS", (int)n);
    ModApi_ShowError(text);
}

void CP_AI_Init(void)
{
    ModApi_AddEntry(MODPOINT_TRANSFER_MARKET, CP_SLUG, "AI TRANSFERS", ai_transfers_now);
}

#endif /* SWOS_SDL_BUILD */
