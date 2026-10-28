/* Exercise the production progress module with real files in a disposable cwd.
 * The Python runner starts fresh processes to test restart persistence. */
#include "challenge_collection.h"
#include "swos_io.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_write;
int swos_io_load_alloc(const uint8_t *name, uint8_t **out, uint32_t *size)
{
    FILE *f = fopen((const char *)name, "rb");
    long n;
    uint8_t *p;
    if (!f) return 1;
    fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
    assert(n >= 0);
    p = malloc((size_t)n + 1);
    assert(p);
    assert(fread(p, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *out = p; *size = (uint32_t)n;
    return 0;
}
void swos_io_free(void *p) { free(p); }
int swos_io_save(const uint8_t *name, const uint8_t *p, uint32_t n)
{
    FILE *f;
    if (fail_write) return 1;
    f = fopen((const char *)name, "wb");
    if (!f) return 1;
    assert(fwrite(p, 1, n, f) == n);
    return fclose(f) != 0;
}

int main(int argc, char **argv)
{
    int index, expected;
    assert(argc == 4);
    index = atoi(argv[2]); expected = atoi(argv[3]);
    ChallengeProgress_Load();
    if (!strcmp(argv[1], "replay")) {
        ChallengeProgress_Begin(index, 1);
        ChallengeProgress_PositionLoaded();
        ChallengeProgress_FinalWhistle(0, 1);
        assert(ChallengeProgress_Result() == COLLECTION_LOST);
        ChallengeProgress_Restart();
        assert(ChallengeProgress_Result() == COLLECTION_ABORTED);
        ChallengeProgress_FinalWhistle(2, 0); /* not loaded yet: no award */
        assert(ChallengeProgress_Result() == COLLECTION_ABORTED);
        ChallengeProgress_PositionLoaded();
        ChallengeProgress_FinalWhistle(2, 0);
        assert(ChallengeProgress_Result() == COLLECTION_WON);
        assert(ChallengeProgress_Count() == expected);
        ChallengeProgress_Restart();
        ChallengeProgress_PositionLoaded();
        ChallengeProgress_FinalWhistle(1, 1);
        assert(ChallengeProgress_Result() == COLLECTION_LOST);
        assert(ChallengeProgress_Count() == expected);
        ChallengeProgress_End();
        ChallengeProgress_Restart(); /* cannot reactivate a finished session */
        ChallengeProgress_PositionLoaded();
        ChallengeProgress_FinalWhistle(2, 0);
        assert(ChallengeProgress_Result() == COLLECTION_LOST);
        return 0;
    }
    if (!strcmp(argv[1], "status")) {
        assert(ChallengeProgress_Count() == expected);
        assert(ChallengeProgress_Recovered() == index);
        return 0;
    }
    ChallengeProgress_Begin(index, !strcmp(argv[1], "away") ? 2 : 1);
    if (strcmp(argv[1], "unloaded")) ChallengeProgress_PositionLoaded();
    if (!strcmp(argv[1], "abort")) ChallengeProgress_End();
    if (!strcmp(argv[1], "retry")) fail_write = 1;
    if (!strcmp(argv[1], "draw")) ChallengeProgress_FinalWhistle(2, 2);
    else if (!strcmp(argv[1], "loss") || !strcmp(argv[1], "away"))
        ChallengeProgress_FinalWhistle(0, 1);
    else ChallengeProgress_FinalWhistle(2, 0);
    assert(ChallengeProgress_Count() == expected);
    if (!strcmp(argv[1], "abort") || !strcmp(argv[1], "unloaded"))
        assert(ChallengeProgress_Result() == COLLECTION_ABORTED);
    else if (!strcmp(argv[1], "draw") || !strcmp(argv[1], "loss"))
        assert(ChallengeProgress_Result() == COLLECTION_LOST);
    else assert(ChallengeProgress_Result() == COLLECTION_WON);
    if (fail_write) {
        assert(ChallengeProgress_Dirty());
        fail_write = 0;
        assert(ChallengeProgress_Save() == 0);
    }
    assert(!ChallengeProgress_Dirty());
    /* Another final-whistle callback cannot reverse or duplicate the result. */
    ChallengeProgress_FinalWhistle(0, 9);
    assert(ChallengeProgress_Count() == expected);
    ChallengeProgress_End();
    return 0;
}
