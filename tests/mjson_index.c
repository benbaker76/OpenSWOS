/* gcc -O2 -DMJSON_ENABLE_PRINT=0 -Iinclude tests/mjson_index.c src/mjson.c
 *     -o build/mjson_index_test && build/mjson_index_test [career.CAR]
 * Compare indexed queries with the original parser, including absent fields,
 * nested paths, duplicate keys, escaped paths, and index lifetime/fallback. */
#include "mjson.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void compare(struct mjson_index *idx, const char *json, int len,
                    const char *path) {
    const char *a = NULL, *b = NULL;
    int al = -1, bl = -1, at, bt;
    mjson_use_index(NULL);
    at = mjson_find(json, len, path, &a, &al);
    mjson_use_index(idx);
    bt = mjson_find(json, len, path, &b, &bl);
    if (at != bt || (at && (al != bl || a != b))) {
        fprintf(stderr, "lookup differs: %s (%d/%d, %d/%d)\n", path, at, bt, al, bl);
        abort();
    }
}

int main(int argc, char **argv) {
    const char *json = " {\"n\":42,\"nil\":null,\"yes\":true,\"no\":false,"
        "\"s\":\"braces } ] and \\\"quotes\\\"\",\"obj\":{\"x\":-7},"
        "\"a\":[{\"x\":3},[4,5],6],\"empty\":{},\"list\":[],"
        "\"a.b\":9,\"dup\":1,\"dup\":2}";
    const char *paths[] = {"$", "$.n", "$.nil", "$.yes", "$.no", "$.s",
        "$.obj", "$.obj.x", "$.obj.absent", "$.a", "$.a[0].x", "$.a[1][1]",
        "$.a[2]", "$.a[3]", "$.a[0].absent", "$.empty", "$.list", "$.list[0]",
        "$.missing", "$.missing.x", "$.n.x", "$.a\\.b", "$.dup", "bad"};
    struct mjson_index_entry entries[128], small[1];
    struct mjson_index idx, nested;
    int len = (int)strlen(json);
    assert(mjson_index_build(&idx, json, len, entries, 128) == 1);
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
        compare(&idx, json, len, paths[i]);
    long value = 0;
    char str[64];
    assert(mjson_get_long(json, len, "$.obj.x", &value) == 1 && value == -7);
    assert(mjson_get_string(json, len, "$.s", str, sizeof str) > 0);
    assert(strcmp(str, "braces } ] and \"quotes\"") == 0);
    /* A different source or a different slice must not use the active index. */
    compare(&idx, "{\"n\":99}", 8, "$.n");
    compare(&idx, json, len - 1, "$.n");
    assert(mjson_index_build(&nested, "{\"n\":99}", 8, small, 1) == 1);
    assert(mjson_use_index(&nested) == &idx);
    assert(mjson_get_long(nested.buf, nested.len, "$.n", &value) && value == 99);
    assert(mjson_use_index(&idx) == &nested);
    assert(mjson_get_long(json, len, "$.n", &value) && value == 42);
    mjson_use_index(NULL);
    assert(mjson_index_build(&nested, json, len, small, 1) == 0);
    assert(nested.buf == NULL);
    compare(&nested, json, len, "$.obj.x");
    mjson_use_index(NULL);
    assert(mjson_index_build(&nested, "{}", 2, small, 0) == 1);
    compare(&nested, "{}", 2, "$.missing");
    mjson_use_index(NULL);
    const char *bad[] = {"", "[1]", "{\"x\":", "{\"x\":[1,2}", "{\"x\":oops}",
                         "{\"x\":\"unterminated}", "{}garbage"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        assert(mjson_index_build(&nested, bad[i], (int)strlen(bad[i]), entries, 128) < 0);
        assert(nested.buf == NULL);
    }
    puts("JSON index compatibility and invalid-input checks passed");
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        assert(fseek(f, 0, SEEK_END) == 0);
        long size = ftell(f);
        assert(size > 0);
        rewind(f);
        char *buf = malloc((size_t)size + 1);
        assert(buf && fread(buf, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
        buf[size] = 0;
        clock_t start = clock();
        assert(mjson_index_build(&idx, buf, (int)size, entries, 128) == 1);
        double build_ms = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
        for (int i = 0; i < idx.count; i++) {
            char path[256];
            const struct mjson_index_entry *e = &entries[i];
            assert(e->key_len + 3 < (int)sizeof path);
            snprintf(path, sizeof path, "$.%.*s", e->key_len, buf + e->key_offset);
            compare(&idx, buf, (int)size, path);
        }
        compare(&idx, buf, (int)size, "$.career_scalars.diy_league_season_state_table");
        compare(&idx, buf, (int)size, "$.selected_teams[0]");
        compare(&idx, buf, (int)size, "$.team_pool");
        compare(&idx, buf, (int)size, "$.stage_titles");
        compare(&idx, buf, (int)size, "$.conti_edition");
        double ms[2];
        for (int indexed = 0; indexed < 2; indexed++) {
            mjson_use_index(indexed ? &idx : NULL);
            start = clock();
            for (int i = 0; i < 64; i++)
                assert(mjson_find(buf, (int)size, "$.absent_section", NULL, NULL) == 0);
            ms[indexed] = 1000.0 * (clock() - start) / CLOCKS_PER_SEC;
        }
        mjson_use_index(NULL);
        printf("Save: %ld bytes, %d sections, all queries agree; index %.2f ms; "
               "64 absent queries %.2f -> %.2f ms (host CPU)\n",
               size, idx.count, build_ms, ms[0], ms[1]);
        free(buf);
    }
    return 0;
}
