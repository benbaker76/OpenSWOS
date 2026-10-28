/* Portable collection progress. Two alternating, checksummed save files leave
 * the previous successful write intact if a disk write is interrupted. No
 * simulation state or process pointers are stored here. */
#include "challenge_collection.h"
#include "swos_io.h"
#include "endian_helpers.h"
#include <string.h>
#ifndef SWOS_SDL_BUILD
/* The freestanding Amiga string header omits this runtime function. */
extern int memcmp(const void *a, const void *b, size_t n);
#endif

#define PROGRESS_BYTES (16 + COLLECTION_COUNT * 4)
static const uint8_t *const names[2] = {
    (const uint8_t *)"CHPROG1.PRG", (const uint8_t *)"CHPROG2.PRG"
};
static uint32_t sequence, completed[COLLECTION_COUNT];
static int slot = -1, dirty, recovered, loaded;
static int active = -1, attempt = -1, human, positionLoaded, result;
static unsigned finalGoals[2];

static uint32_t checksum(const uint8_t *p, unsigned n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

static int decode(const uint8_t *p, uint32_t n)
{
    return n == PROGRESS_BYTES && BE_READ_U32(p) == 0x43485052u &&
        BE_READ_U16(p + 4) == 1 && BE_READ_U16(p + 6) == COLLECTION_COUNT &&
        BE_READ_U32(p + n - 4) == checksum(p, n - 4);
}

void ChallengeProgress_Load(void)
{
    int i;
    if (loaded) return;
    loaded = 1;
    for (i = 0; i < 2; ++i) {
        uint8_t *p = 0;
        uint32_t n = 0;
        if (swos_io_load_alloc(names[i], &p, &n) != 0) continue;
        if (decode(p, n)) {
            uint32_t seq = BE_READ_U32(p + 8);
            if (slot < 0 || (int32_t)(seq - sequence) > 0) {
                int j;
                sequence = seq;
                slot = i;
                for (j = 0; j < COLLECTION_COUNT; ++j)
                    completed[j] = BE_READ_U32(p + 12 + j * 4);
            }
        } else recovered = 1;
        swos_io_free(p);
    }
}

int ChallengeProgress_Save(void)
{
    uint8_t p[PROGRESS_BYTES], *check = 0;
    uint32_t n = 0;
    int i, target = slot == 0 ? 1 : 0;
    if (!dirty) return 0;
    memset(p, 0, sizeof p);
    BE_WRITE_U32(p, 0x43485052u);
    BE_WRITE_U16(p + 4, 1);
    BE_WRITE_U16(p + 6, COLLECTION_COUNT);
    BE_WRITE_U32(p + 8, sequence + 1);
    for (i = 0; i < COLLECTION_COUNT; ++i)
        BE_WRITE_U32(p + 12 + i * 4, completed[i]);
    BE_WRITE_U32(p + sizeof p - 4, checksum(p, sizeof p - 4));
    if (swos_io_save(names[target], p, sizeof p) != 0) return 1;
    if (swos_io_load_alloc(names[target], &check, &n) != 0) return 1;
    i = n != sizeof p || memcmp(p, check, sizeof p) != 0;
    swos_io_free(check);
    if (i) return 1;
    slot = target;
    ++sequence;
    dirty = recovered = 0;
    return 0;
}

int ChallengeProgress_Done(int index)
{
    int i;
    if (index < 0 || index >= COLLECTION_COUNT) return 0;
    for (i = 0; i < COLLECTION_COUNT; ++i)
        if (completed[i] == g_challengeCollection[index].id) return 1;
    return 0;
}

int ChallengeProgress_Count(void)
{
    int i, n = 0;
    for (i = 0; i < COLLECTION_COUNT; ++i) n += ChallengeProgress_Done(i);
    return n;
}
int ChallengeProgress_Dirty(void) { return dirty; }
int ChallengeProgress_Recovered(void) { return recovered; }
int ChallengeProgress_Result(void) { return result; }
unsigned ChallengeProgress_Goals(int side) { return finalGoals[side == 2]; }

void ChallengeProgress_Begin(int index, int humanSide)
{
    attempt = active = index >= 0 && index < COLLECTION_COUNT ? index : -1;
    human = humanSide;
    positionLoaded = 0;
    result = COLLECTION_ABORTED;
    finalGoals[0] = finalGoals[1] = 0;
}
void ChallengeProgress_PositionLoaded(void) { positionLoaded = 1; }
void ChallengeProgress_Restart(void)
{
    /* The final whistle disarms active, but the attempt's identity survives
     * until leaving Challenge_Play. A retry must be eligible to complete too. */
    if (attempt >= 0) ChallengeProgress_Begin(attempt, human);
}
void ChallengeProgress_End(void) { attempt = active = -1; positionLoaded = 0; }

void ChallengeProgress_FinalWhistle(unsigned goals1, unsigned goals2)
{
    int i, index = active;
    if (index < 0 || !positionLoaded || (human != 1 && human != 2)) return;
    active = -1; /* A repeated end event cannot award another completion. */
    finalGoals[0] = goals1;
    finalGoals[1] = goals2;
    result = (human == 1 ? goals1 > goals2 : goals2 > goals1)
        ? COLLECTION_WON : COLLECTION_LOST;
    if (result != COLLECTION_WON) return;
    if (!ChallengeProgress_Done(index)) {
        for (i = 0; i < COLLECTION_COUNT; ++i) {
            if (!completed[i]) {
                completed[i] = g_challengeCollection[index].id;
                dirty = 1;
                break;
            }
        }
    }
    (void)ChallengeProgress_Save();
}
