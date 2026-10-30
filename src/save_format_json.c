/*
 * save_format_json.c — JSON serializer/parser for SWOS save files.
 *
 * Five save kinds, all fully structured (no base64 blobs):
 *   - career      (.CAR) — SaveJson_WriteCareer / SaveJson_LoadCareer
 *   - preset      (.PRE) — write/load_competition_buf via SaveJson_*Preset
 *   - DIY         (.DIY) — same path with KIND_DIY
 *   - highlights  (.HIL) — SaveJson_WriteHighlights / SaveJson_LoadHighlights
 *   - tactics     (.TAC) — SaveJson_WriteTactics / SaveJson_LoadTactics
 *
 * Every byte that round-trips is covered by either a typed scalar
 * (s_career_scalars FLD_* entries) or a structured top-level field
 * (selected_teams, tmd_teams, career_foreign_market_players,
 * career_team_reserves, current_team_reserves, scenes, etc.). The
 * legacy `opaque_data` blob and the `blobs` sub-object are gone.
 *
 * See SAVE_FORMAT_JSON.md for the migration history.
 */

#include "save_format_json.h"
#include "mjson.h"
#include "game_types.h"
#include "endian_helpers.h"
#include "world.h"             /* "world" in career and competition saves */
#include "team_pool.h"         /* "team_pool" in career saves */
#include "world_playoff.h"     /* "world_playoff" in career saves */
#include "uefa_coef.h"         /* "uefa_coef", "uefa_qualifying" in career saves */
#include "league_playoffs.h"   /* "league_stages" in career saves */
#include "world_conti.h"       /* "conti_holders", "conti_edition" in career saves */
#include "uefa_access.h"
#include "world_cups.h"        /* "world_cups" in career saves */
#include "world_cupd.h"        /* "world_cupd" in career saves */
#include "comp_identity.h"     /* COMPID_copaLibertadores: an old save's Copa (128) */
#include "translations.h"   /* tr() — localize competition name symbols */

extern uint8_t *_currentCompetitionPtr;

/* Country-code byte of the primary competition's static struct at save
 * time. Rebuild_CompetitionSlotPointers uses this when selTeamNumber
 * is the 0xFFFF "no team" sentinel — i.e. season mode with all-CPU
 * teams, where there's no human team identifying the league. Without
 * this scalar the rebuild can't re-derive currentCompetitionPtr from
 * competitionsTable[] and the contest menu segfaults on first draw.
 * Default 0xFF = "no comp" (career was at front-end / nothing
 * selected yet). */
static uint8_t s_loaded_primary_country_code = 0xFF;
uint8_t SaveJson_GetLoadedPrimaryCountryCode(void)
{
    return s_loaded_primary_country_code;
}

#include <string.h>
#ifdef SWOS_SDL_BUILD
#include <stdlib.h>  /* atoi */
#include <stdio.h>   /* DataJson self-test */
#endif
#ifdef SWOS_SAVE_FORMAT_JSON_PROFILE
/* clock() — only the PROFILE macros need this. The Amiga toolchain
 * has no <time.h>, so this also keeps that include off-path unless
 * profiling is explicitly enabled. */
#include <time.h>
#endif
#include "swos_types.h"
#include "swos_io.h"

/* Amiga m68k <string.h> is partial — declares memcpy but not
 * strlen / strcmp / memcmp. And there is no <stdlib.h> at all, so atoi
 * is undeclared too. Provide explicit prototypes for the strict
 * builtin-mismatch check; the symbols themselves resolve at link
 * time via libc / gcc8_c_support. */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)
extern size_t strlen(const char *s);
extern int    strcmp(const char *a, const char *b);
extern int    memcmp(const void *a, const void *b, size_t n);
extern int    atoi(const char *s);
#endif

/* Profile helpers — only emit when SWOS_SAVE_FORMAT_JSON_PROFILE is
 * defined. Times each labeled phase in microseconds via clock(),
 * which is good enough resolution for the kind of work we're
 * measuring (multi-millisecond JSON parse / file I/O). */
#ifdef SWOS_SAVE_FORMAT_JSON_PROFILE
#define PROF_START(name) clock_t prof_##name = clock()
#define PROF_END(name, label) do { \
    clock_t _e = clock(); \
    double _ms = (double)(_e - prof_##name) * 1000.0 / (double)CLOCKS_PER_SEC; \
    SWOS_DBG("[json-prof-%s] %s: %.2f ms\n", \
            (label), #name, _ms); \
    SWOS_DBG_FLUSH(); \
} while (0)
#else
#define PROF_START(name) ((void)0)
#define PROF_END(name, label) ((void)0)
#endif

/* careerFileBuffer (TMD team-pool count) + numSelectedTeams are standalone
 * uint16 globals (the careerFileBufferBuf overlay was retired). careerFileBuffer
 * was historically declared uint8_t here (a "stub" for the old byte-memset);
 * it is really a uint16, so declare it as such — `careerFileBuffer = 0` must
 * clear both bytes, and tmd_team_count reads it FLD_U16. */
extern uint16_t _careerFileBuffer;
extern uint16_t _numSelectedTeams;
/* step-4 phase 3: standings/bracket team-selection fields hold a team IDENTITY
 * (-1 == careerTeam, >= 0 == selTeamsPtr index), not a byte offset. The load
 * reconstruction below must store the identity, via this helper (diy_competition.c). */
extern int32_t  diy_team_to_identity(const uint8_t *teamPtr);
extern uint8_t  _seasonInformationsArray[];

/* Externs for the other save buffers — used by future sub-files but
 * referenced here so the linker keeps the symbols available.
 * (competitionFileBuffer retired → primaryCompetitionBuf, declared below; S6) */
extern DIYTournamentState _diyFileBufferCopy;

/* file_io.c writer helper. */
extern int16_t WriteFile(uint8_t *filename, uint8_t *src, uint32_t size);
extern void    ShowFsErrorByCode(uint16_t code);

#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
/* Generic round-trip verify: snapshot a memory region, zero it,
 * call the just-written file's loader, then diff against the
 * snapshot. Used by each Write* path to confirm the JSON schema
 * round-trips bit-exactly for that save type.
 *
 * NON-RESTORE POLICY: the loader (reload) writes the LIVE game globals and, as
 * a data-load, also rebuilds runtime/derived state (diyCompetitionStart, the
 * re-derived pointers, reconstructed comp-slot caches, ...). We intentionally
 * leave that fully-reloaded state in place rather than copying the pre-save
 * snapshot back. A PARTIAL restore (snapshot only this region / only the
 * scalar table) was the bug: it left the restored bytes pre-save but the
 * runtime caches reloaded, so a save-then-CONTINUE walked into a desynced mix
 * and panicked (e.g. FindTeamInDIY 0x6006). The fully-reloaded state is
 * exactly what a fresh load of the same file produces — proven playable — so
 * continuing from it is safe. (For a clean verify the reloaded bytes equal the
 * pre-save bytes, so authored state is unchanged either way.) The snapshot is
 * kept only for the diff. VERIFY builds therefore behave like an implicit
 * save+reload on every save, which is acceptable for a debug/test build. */
typedef int16_t (*VerifyReloadFn)(uint8_t *filename);

/* Optional per-region predicate: return non-zero for byte offsets that are
 * EXPECTED to differ across a round-trip and must not count as mismatches —
 * i.e. pointer-to-disk fields that are serialized symbolically (the content
 * round-trips, the raw pointer value cannot). NULL = compare every byte. */
typedef int (*VerifyIgnoreFn)(uint32_t offset);

static void verify_roundtrip_region(const char *label,
                                    uint8_t *region, uint32_t size,
                                    VerifyReloadFn reload,
                                    uint8_t *filename,
                                    VerifyIgnoreFn ignore)
{
    uint8_t *snap = (uint8_t *)swos_io_alloc(size);
    if (!snap) return;
    memcpy(snap, region, size);
    memset(region, 0, size);

    reload(filename);

    size_t mismatches = 0;
    size_t first = (size_t)-1;
    for (uint32_t i = 0; i < size; i++) {
        if (region[i] != snap[i]) {
            if (ignore && ignore(i)) continue;   /* expected-symbolic byte */
            if (mismatches < 20) {
                if (first == (size_t)-1) first = i;
                SWOS_DBG(
                        "[json-verify-%s] mismatch at +%u: loaded=0x%02X saved=0x%02X\n",
                        label, (unsigned)i,
                        (unsigned)region[i], (unsigned)snap[i]);
            }
            mismatches++;
        }
    }
    if (mismatches == 0)
        SWOS_DBG("[json-verify-%s] 0 mismatches across %u bytes\n",
                 label, (unsigned)size);
    else
        SWOS_DBG("[json-verify-%s] %zu mismatches across %u bytes (first at +%zu)\n",
                 label, mismatches, (unsigned)size, first);

    /* Deliberately NOT restoring `snap` over `region` — see NON-RESTORE POLICY
     * above. The live state stays as the fresh reload (consistent + playable). */
    swos_io_free(snap);
}

/* Reload wrappers with the simpler `(filename)` signature so the
 * verify helper can take a uniform function pointer. */
static int16_t verify_reload_career(uint8_t *f)
{
    return SaveJson_LoadCareer(f, NULL, 0);
}
static int16_t verify_reload_preset(uint8_t *f)
{
    return SaveJson_LoadPreset(f, NULL, 0);
}
static int16_t verify_reload_diy(uint8_t *f)
{
    return SaveJson_LoadDIY(f, NULL, 0);
}
static int16_t verify_reload_highlights(uint8_t *f)
{
    return SaveJson_LoadHighlights(f, NULL, 0);
}
#endif

/* JSON saves can grow to several hundred KB (>3x the legacy binary
 * size from base64 expansion + key text), which overflows the
 * careerFileBuffer destination if we route through LoadFileEx.
 * Delegate to swos_io.c, which bypasses the asset-loader staging and
 * returns a freshly-allocated buffer:
 *   SDL:   malloc + stdio read.
 *   Amiga: AllocMemAny + RobNorthenLoader_C dispatch. */
static int read_save_file_heap(const uint8_t *filename,
                               uint8_t **out_buf, uint32_t *out_size)
{
    return swos_io_load_alloc(filename, out_buf, out_size);
}

/* String pool for save-load round-trips of in-memory string pointer
 * fields (the four comp-name slots in seasonInformations). On save we
 * emit the string content as a bare JSON value; on load we copy the
 * content into this pool and write the pool address into the slot.
 * Display code (StringCopy etc.) just walks bytes to NUL, so any
 * valid backing memory works. Pool is reset at the start of each
 * save AND each load so the same string always interns at the same
 * pool offset — byte-stable round-trip. */
#define STRING_POOL_SIZE 8192u
static char     s_string_pool[STRING_POOL_SIZE];
static uint32_t s_string_pool_used = 0;

static void string_pool_reset(void)
{
    s_string_pool_used = 0;
}

static uintptr_t string_pool_intern(const char *s)
{
    uint32_t len = (uint32_t)strlen(s);
    if (s_string_pool_used + len + 1 > STRING_POOL_SIZE)
        return 0u;
    char *dst = &s_string_pool[s_string_pool_used];
    memcpy(dst, s, (size_t)len + 1);
    s_string_pool_used += len + 1;
    return (uintptr_t)dst;
}

/* Emit a pointer-to-string field as a bare JSON string. Used for
 * fields that are typed `uint32_t namePtr` in memory but semantically
 * hold a comp-name / division-name string. Dereferences the pointer,
 * walks up to 128 bytes for the NUL terminator, and emits a plain
 * JSON string — no {sym}/{str}/{raw} discriminator wrapper.
 *
 * Null / unreadable pointers emit as an empty string; the load side
 * leaves the slot at 0 in that case so consumers (statistics.c:1930)
 * still short-circuit on NULL. */
static void emit_str_pool_field(mjson_print_fn_t fn, void *fnd,
                                const char *key, const uint8_t *val)
{
    if (val == NULL) {
        mjson_printf(fn, fnd, "%Q:%Q", key, "");
        return;
    }
    const char *s = (const char *)val;
    size_t n = 0;
    while (n < 128u && s[n] != '\0') n++;
    if (n == 0 || n >= 128u) {
        mjson_printf(fn, fnd, "%Q:%Q", key, "");
        return;
    }
    mjson_printf(fn, fnd, "%Q:%.*Q", key, (int)n, s);
}

/* Read a bare JSON string field, intern into the string pool, store
 * the resulting pool address into *out. Empty / missing string leaves
 * *out at NULL. */
static void load_str_pool_field(const char *buf, int buflen,
                                const char *path, uint8_t **out)
{
    *out = NULL;
    char tmp[128] = {0};
    int got = mjson_get_string(buf, buflen, path, tmp, sizeof(tmp));
    if (got <= 0 || tmp[0] == '\0') return;
    *out = (uint8_t *)string_pool_intern(tmp);
}

/* ---------- format constants ---------- */

/* On-disk JSON save schema version. Written into every save
 * ("schema_version") and verified on load — a mismatch makes the load
 * cleanly fail (returns error, no crash / no silent mis-decode) rather
 * than feeding a stale-layout save into the game.
 *
 * BUMP THIS on EVERY change to the on-disk format: a new/removed/renamed
 * field, a field's encoding changing (e.g. FLD_BYTES -> FLD_U16/U32_ARRAY),
 * a region's size/layout shifting, etc. The whole point is that an old
 * save can never be loaded by a newer binary that would misread it.
 *
 * History:
 *   1 — initial structured JSON format.
 *   2 — cross-target (64-bit SDL <-> 32-bit BE Amiga) rework: typed
 *       competition-slot serializer (comp_slots), FLD_U16_ARRAY /
 *       FLD_U32_ARRAY endian-neutral arrays (weekly_wage_by_player_index,
 *       board_confidence_*, removed_player_stat_accum, comp header runs),
 *       comp_copy template reconstruction, cup team_offset_table symbolic
 *       encoding, context-slot overlay pads. Saves from v1 are rejected.
 *   3 — cup fixes: teamSelectionKeyCache stride re-lay (cache_strides) +
 *       game-record score fields packedGoals/aggregate/penaltyScore
 *       endian-neutral (cache_scores, fixes the swapped VFL BOCHUM 1-0/0-1
 *       score), cup team_grid_pairings moved into comp_slot (struct-
 *       anchored, fixes the +8 cross-target offset shift / "TEAM v TEAM"
 *       bracket) + cup knockout score arrays (cup_score_tab/agg/pen,
 *       struct-anchored endian-neutral int16, fixes scrambled bracket
 *       scores) + the WHOLE matchData union (matchdata_raw, struct-anchored
 *       0xEA byte blob — fixes the +8 cross-target layout shift for every
 *       matchData field at once: cup played-flags, scores, leagueTeamIds,
 *       teamEntryOffsets, round info; cup_score_* layers endian-neutral
 *       scores on top). Saves from v2 are rejected.
 */
#define SAVE_JSON_SCHEMA_VERSION 3

/* The football world a save was made in (include/world.h): written by the career and
 * competition saves as "world"; a save without it is 96/97, the world before worlds. A
 * save of another world is refused: its teams and competitions are not this one's. */
static int save_world_ok(const uint8_t *src, uint32_t size)
{
    char tag[16] = {0};
    const int32_t w = mjson_get_string((const char *)src, (int)size, "$.world", tag, sizeof tag) > 0
                    ? World_FromTag(tag) : (int32_t)WORLD_9697;
    if (w == (int32_t)World_Current())
        return 1;
    SWOS_DBG("[save] made in world %s, this is %s: refused\n",
             tag[0] ? tag : "9697", World_Tag());
    return 0;
}

#define KIND_CAREER     "career"
#define KIND_PRESET     "preset"
#define KIND_DIY        "diy"
#define KIND_HIGHLIGHTS "highlights"
#define KIND_TACTICS    "tactics"

/* (career_data_size() retired with the careerFileBufferBuf overlay — the save is
 * typed JSON of named fields; there is no contiguous "career region" left, and
 * careerFileBuffer/numSelectedTeams are now standalone scalars.) */

/* ---------- format detection ---------- */

int SaveJson_BufferIsJson(const uint8_t *buf, uint32_t size)
{
    if (!buf) return 0;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = buf[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        return (c == '{') ? 1 : 0;
    }
    return 0;
}

/* ---------- mjson print sink: writes to a heap-grown buffer ---------- */

typedef struct {
    char    *ptr;
    size_t   len;
    size_t   cap;
    int      oom;
} JsonOutBuf;

static int json_out_emit(const char *chunk, int chunk_len, void *ud)
{
    JsonOutBuf *o = (JsonOutBuf *)ud;
    if (o->oom) return 0;
    if (o->len + (size_t)chunk_len + 1 > o->cap) {
        size_t want = (o->cap ? o->cap : 4096);
        while (want < o->len + (size_t)chunk_len + 1) want *= 2;
        char *grew = (char *)swos_io_realloc(o->ptr, want);
        if (!grew) { o->oom = 1; return 0; }
        o->ptr = grew;
        o->cap = want;
    }
    memcpy(o->ptr + o->len, chunk, (size_t)chunk_len);
    o->len += (size_t)chunk_len;
    o->ptr[o->len] = '\0';
    return chunk_len;
}

/* ---------- seasonInformationsArray semantic encoder/decoder ----------
 *
 * Each 106-byte record is laid out per the SI_* defines in
 * statistics.c. The 4 pointer-typed string slots (offsets 22/26/30/34)
 * are emitted as bare JSON strings via emit_str_pool_field and
 * re-interned into the string pool on load. Empty / missing slots
 * round-trip to NULL pointers; the consumer (statistics.c:1930)
 * already short-circuits on those.
 *
 * Field offsets — kept private to this file. Mirrors the legacy
 * SI_* defines but local so we don't drag statistics.c into the
 * include graph. */
#define SAVE_SI_ENTRY_SIZE       106u
#define SAVE_SI_ENTRY_COUNT       35u   /* = CAREER_MAX_SEASONS; reader is index-bounded so old
                                         * 20-entry saves still load (extra rows stay zeroed) */
#define SAVE_SI_CLUBNO             0u
#define SAVE_SI_CLUBNAME           2u
#define SAVE_SI_TROPHYFLAGS       20u
#define SAVE_SI_LEAGUESTRPTR      22u
#define SAVE_SI_STRPTR1           26u
#define SAVE_SI_STRPTR2           30u
#define SAVE_SI_STRPTR3           34u
#define SAVE_SI_BESTSCORERNUMGOALS 38u
#define SAVE_SI_BESTSCORERNAME    40u
#define SAVE_SI_2NDSCORERNAME     63u
#define SAVE_SI_COMP1RESULT       86u
#define SAVE_SI_COMP2RESULT       87u
#define SAVE_SI_COMP3RESULT       88u
#define SAVE_SI_COMP4RESULT       89u
#define SAVE_SI_TOTALGOALSSCORED  90u
#define SAVE_SI_TOTALGOALSTAKEN   92u
#define SAVE_SI_TOTALGAMES        94u
#define SAVE_SI_TOTALWINS         95u
#define SAVE_SI_TOTALDRAWN        96u
#define SAVE_SI_TOTALLOST         97u
#define SAVE_SI_FRIENDLYWINS      98u
#define SAVE_SI_FRIENDLYDRAWS     99u
#define SAVE_SI_FRIENDLYLOST     100u
#define SAVE_SI_TOURWINS         101u
#define SAVE_SI_TOURDRAWS        102u
#define SAVE_SI_TOURLOST         103u
#define SAVE_SI_TOURNAMENTSTATUS 104u
#define SAVE_SI_MANAGERSTATUS    105u

/* career_verify_ignore RETIRED (UNTANGLE B4): it exempted only the 4
 * seasonInformations comp-name pointer-to-disk slots from the career byte-diff.
 * seasonInformationsArray now lives outside the [careerFileBuffer,
 * numSelectedTeams) VERIFY region (career_season_info.c), so those bytes are no
 * longer diffed and no exemption is needed — the verify call passes NULL. */

/* The previous {sym}/{str}/{raw} discriminator wrapper used for
 * pointer-typed fields has been retired — all top-level pointer
 * fields are gone from the format and the only remaining pointer-
 * shaped slots (4 × seasonInformations comp-name strings) now use
 * emit_str_pool_field / load_str_pool_field which emit a bare JSON
 * string. The LUT-walking sym_from_ptr / ptr_from_sym helpers and
 * the looks_sentinel / looks_readable / looks_pool heuristics that
 * supported it are gone with it. */

static void emit_string_field(mjson_print_fn_t fn, void *fnd, const char *key,
                              const uint8_t *src, uint32_t maxlen)
{
    /* The SWOS strings are NUL-terminated within a fixed-width slot.
     * Trim to the shorter of strlen or maxlen for the JSON emit. */
    uint32_t n = 0;
    while (n < maxlen && src[n] != 0) n++;
    mjson_printf(fn, fnd, "%Q:%.*Q", key, (int)n, (const char *)src);
}

static void load_string_field(const char *buf, int buflen, const char *path,
                              uint8_t *dst, uint32_t maxlen)
{
    /* Zero the slot first so unset fields keep the NUL terminator. */
    memset(dst, 0, maxlen);
    char tmp[128];
    int got = mjson_get_string(buf, buflen, path,
                               tmp, (int)sizeof(tmp));
    if (got <= 0) return;
    uint32_t n = (uint32_t)got;
    if (n > maxlen - 1) n = maxlen - 1;
    memcpy(dst, tmp, n);
}

/* Write season_informations as a JSON array of 20 entries.
 *
 * Walks the in-memory array via the typed SeasonInformations struct so
 * field offsets stay consistent across 32-bit (row size 106) and 64-bit
 * (row size 122). The on-disk JSON keys are semantic and identical on
 * both targets — a save written on one target loads cleanly on the
 * other. */
static void emit_season_informations(mjson_print_fn_t fn, void *fnd)
{
    const SeasonInformations *arr =
        (const SeasonInformations *)_seasonInformationsArray;
    mjson_printf(fn, fnd, "%Q:[", "season_informations");
    for (uint32_t i = 0; i < SAVE_SI_ENTRY_COUNT; i++) {
        const SeasonInformations *e = &arr[i];

        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        mjson_printf(fn, fnd, "%s", "{");

        mjson_printf(fn, fnd, "%Q:%u,", "club_no", (unsigned)e->clubNo);
        emit_string_field(fn, fnd, "club_name",
                          (const uint8_t *)e->clubName, sizeof(e->clubName));
        mjson_printf(fn, fnd, ",%Q:%u,",
                     "trophy_flags", (unsigned)e->trophyFlags);

        /* Four per-season comp-name strings. */
        emit_str_pool_field(fn, fnd, "league_str", e->leagueStrPtr);
        mjson_printf(fn, fnd, "%s", ",");
        emit_str_pool_field(fn, fnd, "str_1", e->strPtr1);
        mjson_printf(fn, fnd, "%s", ",");
        emit_str_pool_field(fn, fnd, "str_2", e->strPtr2);
        mjson_printf(fn, fnd, "%s", ",");
        emit_str_pool_field(fn, fnd, "str_3", e->strPtr3);

        mjson_printf(fn, fnd, ",%Q:%u,",
                     "best_scorer_goals", (unsigned)e->bestScorerNumGoals);
        emit_string_field(fn, fnd, "best_scorer_name",
                          (const uint8_t *)e->bestScorerName,
                          sizeof(e->bestScorerName));
        mjson_printf(fn, fnd, "%s", ",");
        emit_string_field(fn, fnd, "second_scorer_name",
                          (const uint8_t *)e->secondScorerName,
                          sizeof(e->secondScorerName));

        mjson_printf(fn, fnd,
            ",%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
            "%Q:%u,%Q:%u,"
            "%Q:%u,%Q:%u,%Q:%u,%Q:%u,"
            "%Q:%u,%Q:%u,%Q:%u,"
            "%Q:%u,%Q:%u,%Q:%u,"
            "%Q:%u,%Q:%d}",
            "comp_1_result", (int)e->comp1Result,
            "comp_2_result", (int)e->comp2Result,
            "comp_3_result", (int)e->comp3Result,
            "comp_4_result", (int)e->comp4Result,
            "total_goals_scored", (unsigned)e->totalGoalsScored,
            "total_goals_taken",  (unsigned)e->totalGoalsTaken,
            "total_games",   (unsigned)e->totalGames,
            "total_wins",    (unsigned)e->totalWins,
            "total_drawn",   (unsigned)e->totalDrawn,
            "total_lost",    (unsigned)e->totalLost,
            "friendly_wins",  (unsigned)e->friendlyWins,
            "friendly_draws", (unsigned)e->friendlyDraws,
            "friendly_lost",  (unsigned)e->friendlyLost,
            "tour_wins",   (unsigned)e->tourWins,
            "tour_draws",  (unsigned)e->tourDraws,
            "tour_lost",   (unsigned)e->tourLost,
            "tournament_status", (unsigned)e->tournamentStatus,
            "manager_status", (int)e->managerStatus);
    }
    mjson_printf(fn, fnd, "%s", "]");
}

/* Parse `season_informations` array from JSON, overwriting the 20
 * records in memory at `seasonInformationsArray`. Called AFTER the
 * opaque_data base64 has been decoded so we override the affected
 * bytes (including stale pointers) with semantic values. */
static void load_season_informations(const char *buf, int buflen)
{
    string_pool_reset();

    /* Slice the array once, then walk each entry as its own
     * sub-object. Cuts ~400 full-file mjson_get_* scans down to
     * 20 array lookups + per-entry scans of ~500 bytes. */
    const char *arr = NULL;
    int arr_len = 0;
    if (mjson_find(buf, buflen, "$.season_informations",
                   &arr, &arr_len) != MJSON_TOK_ARRAY)
        return;

    /* Walk the array via mjson_next so each iteration is O(1) in
     * array position. Indexed "$[i]" finds re-walk from the start
     * each time → O(n²) over the whole array. */
    SeasonInformations *si_arr = (SeasonInformations *)_seasonInformationsArray;
    long num = 0.0;
    int pos = 0, koff, klen, voff, vlen, vt;
    uint32_t i = 0;
    while (i < SAVE_SI_ENTRY_COUNT &&
           (pos = mjson_next(arr, arr_len, pos,
                             &koff, &klen, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) { i++; continue; }
        const char *e_slice = arr + voff;
        int e_slen = vlen;

        SeasonInformations *e = &si_arr[i];
        i++;

        if (mjson_get_long(e_slice, e_slen, "$.club_no", &num) <= 0)
            continue;
        e->clubNo = (uint16_t)num;

        load_string_field(e_slice, e_slen, "$.club_name",
                          (uint8_t *)e->clubName, sizeof(e->clubName));

        if (mjson_get_long(e_slice, e_slen, "$.trophy_flags", &num) > 0)
            e->trophyFlags = (uint16_t)num;

        load_str_pool_field(e_slice, e_slen, "$.league_str", &e->leagueStrPtr);
        load_str_pool_field(e_slice, e_slen, "$.str_1",      &e->strPtr1);
        load_str_pool_field(e_slice, e_slen, "$.str_2",      &e->strPtr2);
        load_str_pool_field(e_slice, e_slen, "$.str_3",      &e->strPtr3);

        if (mjson_get_long(e_slice, e_slen, "$.best_scorer_goals", &num) > 0)
            e->bestScorerNumGoals = (uint16_t)num;

        load_string_field(e_slice, e_slen, "$.best_scorer_name",
                          (uint8_t *)e->bestScorerName,
                          sizeof(e->bestScorerName));
        load_string_field(e_slice, e_slen, "$.second_scorer_name",
                          (uint8_t *)e->secondScorerName,
                          sizeof(e->secondScorerName));

#define LOAD_S8(key, dst) do { \
            if (mjson_get_long(e_slice, e_slen, "$." key, &num) > 0) \
                (dst) = (int8_t)num; \
        } while (0)
#define LOAD_U8(key, dst) do { \
            if (mjson_get_long(e_slice, e_slen, "$." key, &num) > 0) \
                (dst) = (uint8_t)num; \
        } while (0)
#define LOAD_U16(key, dst) do { \
            if (mjson_get_long(e_slice, e_slen, "$." key, &num) > 0) \
                (dst) = (uint16_t)num; \
        } while (0)

        LOAD_S8 ("comp_1_result",      e->comp1Result);
        LOAD_S8 ("comp_2_result",      e->comp2Result);
        LOAD_S8 ("comp_3_result",      e->comp3Result);
        LOAD_S8 ("comp_4_result",      e->comp4Result);
        LOAD_U16("total_goals_scored", e->totalGoalsScored);
        LOAD_U16("total_goals_taken",  e->totalGoalsTaken);
        LOAD_U8 ("total_games",        e->totalGames);
        LOAD_U8 ("total_wins",         e->totalWins);
        LOAD_U8 ("total_drawn",        e->totalDrawn);
        LOAD_U8 ("total_lost",         e->totalLost);
        LOAD_U8 ("friendly_wins",      e->friendlyWins);
        LOAD_U8 ("friendly_draws",     e->friendlyDraws);
        LOAD_U8 ("friendly_lost",      e->friendlyLost);
        LOAD_U8 ("tour_wins",          e->tourWins);
        LOAD_U8 ("tour_draws",         e->tourDraws);
        LOAD_U8 ("tour_lost",          e->tourLost);
        LOAD_U8 ("tournament_status",  e->tournamentStatus);
        LOAD_S8 ("manager_status",     e->managerStatus);
#undef LOAD_S8
#undef LOAD_U8
#undef LOAD_U16
    }
}

/* careerFileBuffer top-level pointer fields are NOT saved.
 *
 * Removed:
 *   currentCompetitionPtr / secondaryCompetitionPtr / tertiaryCompetitionPtr /
 *   quaternaryCompetitionPtr / diyCompetitionPtr / diyCompetitionTablePtr
 *   careerCompetitionSlotPtrTable[5] / careerCupPickList[5]
 *   primary/secondary/tertiary/quaternaryCompStringOffset
 *
 * All re-derivable from non-pointer state on load via
 * RebuildPointersAfterLoad() in career_init.c:
 *   - slot ptr table: refresh from the 5 named per-slot buffer symbols
 *   - top-level comp pointers: look up by selTeamNumber / isNationalSelector
 *     in competitionsTable / careerCupPickList
 *   - cup pick list: re-populated by Career_SetCupPickList_ForSelectedRegion
 *     for national selectors; unused otherwise
 *   - string offsets: zeroed (consumers tolerate NULL as empty string)
 *
 * Previously these were emitted as `{sym: "name"}` references which made
 * the format "pointer-portable" but still pointer-shaped on disk. The
 * roadmap calls for no pointer fields in the JSON; derive everything
 * from a stable identity on load instead.
 */

/* ---------- table-driven scalar/array fields ----------
 *
 * Most of careerFileBuffer is plain typed data — flags, counters,
 * balances, identifiers. Rather than writing a `mjson_printf` line
 * per field (hundreds of them), we describe each field with a
 * struct entry and iterate at save/load time. Saves the JSON
 * structure self-describing, keeps the code small, and adding a
 * field is a one-line table extension.
 *
 * Field types:
 *   FLD_U8 / FLD_S8 / FLD_U16 / FLD_S16 / FLD_U32 / FLD_S32  — typed
 *     scalar at `offset`, emitted as a JSON number.
 *   FLD_STR — char[`size`] NUL-terminated string at `offset`.
 *   FLD_BYTES — `size` bytes at `offset`, emitted as a JSON array of
 *     unsigned ints (small) so it stays human-readable.
 *
 * The table covers fields we've semantically modeled; everything
 * else still rides along in opaque_data until we promote it.
 */
typedef enum {
    FLD_END = 0,
    FLD_U8, FLD_S8,
    FLD_U16, FLD_S16,
    FLD_U32, FLD_S32,
    FLD_STR,
    FLD_BYTES,
    /* `size`/2 endian-NEUTRAL 16-bit words at `addr`, emitted as a JSON
     * array of decimal numbers. Use this — never FLD_BYTES — for any
     * region holding uint16 values that must round-trip across a 32/64-bit
     * AND big/little-endian target boundary: FLD_BYTES dumps raw memory
     * bytes, which carry the saving host's byte order, so a uint16 saved
     * on x86 (LE) loads byte-swapped on the Amiga (BE). Decimal JSON
     * numbers are host-order-independent. (See the primary-competition
     * header runs.) */
    FLD_U16_ARRAY,
    /* `size`/4 endian-NEUTRAL 32-bit words at `addr`, emitted as decimal
     * JSON numbers. Same rationale as FLD_U16_ARRAY but for int32/uint32
     * arrays. CRITICAL: use this — never FLD_BYTES — for arrays the game
     * code declares/uses as int32_t[] even if the save historically dumped
     * them as bytes. FLD_BYTES on an int32 array byte-swaps every element
     * cross-target (e.g. weeklyWageByPlayerIndex: a 64-bit LE save loaded
     * on the BE Amiga turned wage 702=0x000002BE into 0xBE020000, summing
     * to garbage → totalTeamValue overflow → panic in the gate-receipts
     * divu). `size` is in BYTES (size/4 words emitted). */
    FLD_U32_ARRAY,
    /* Split one packed byte at `offset` into two editable JSON numbers:
     * FLD_NIBBLE_HI = bits 4-7, FLD_NIBBLE_LO = bits 0-3. Two StructFieldDesc
     * entries point at the SAME offset (a hi + a lo). Load is read-modify-write,
     * so the byte round-trips exactly as long as both halves are present. Used for
     * the PlayerFile skill pairs (velocity/heading, tackling/control, speed/finishing). */
    FLD_NIBBLE_HI, FLD_NIBBLE_LO,
    /* One contiguous bit-run of the byte at `offset`, selected by `size` used as a
     * MASK (e.g. 0xE0). Emitted right-justified: (byte & mask) >> trailing_zeros(mask);
     * loaded read-modify-write. Several entries at the same offset (whose masks tile the
     * byte) split a packed byte into editable named fields (position/face/flags,
     * cards/injury/status_bucket). */
    FLD_BITS,
} FieldType;

typedef struct {
    const char *name;
    void       *addr;       /* absolute address — taken via &symbol;
                             * 64-bit-stable for standalone overlay
                             * globals, and survives layout shifts in
                             * the careerFileBuffer overlay */
    uint32_t    size;       /* for FLD_STR / FLD_BYTES; ignored for scalars */
    FieldType   type;
    uint8_t     comp;       /* 0 = career-only.
                             * 1 = member of the .DIY/.PRE/.SEA competition-slot
                             *     save set (the primary slot + save-file header +
                             *     counts). Zeroed before a load, then restored.
                             * 2 = also in that save set, but NOT zeroed before a
                             *     load — the settings block (user tactics + the
                             *     options the file carries). A .DIY/.PRE written
                             *     before those keys existed simply has no value
                             *     to restore, and zeroing would WIPE the live
                             *     tactics instead of leaving them alone.
                             * Selected EXPLICITLY by identity for the preset/DIY
                             * save path, NOT by a runtime address span — see
                             * emit_scalar_table_comp. The career save path
                             * ignores this flag and emits the whole table. */
} FieldDesc;

/* The league offsets (diy_league_season_state_table) are written at the original's 2000 when
 * nothing past it is in use -- every 96/97 career -- so a save is the same from a build with the
 * worlds (GLOBAL_TEAM_NUMBERS 2560) and one without them (2000, SWOS_WORLDS=0); a world's
 * career writes them all. The load reads either length (the rest zero). */
static uint32_t emitted_len(const FieldDesc *f, const uint8_t *p)
{
    if (f->size <= 2000 || strcmp(f->name, "diy_league_season_state_table") != 0)
        return f->size;
    for (uint32_t i = 2000; i < f->size; i++)
        if (p[i])
            return f->size;
    return 2000;
}


#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
/* Per-field round-trip verify — defined after field_byte_size, but the career
 * write path (above) calls it, so forward-declare here. */
static void verify_roundtrip_fields(const char *label, const FieldDesc *table,
                                    VerifyReloadFn reload, uint8_t *filename);
#endif

/* extern declarations for all overlay symbols referenced by the
 * tables below. The .equ aliases in swos_globals_career_overlay.c
 * make these visible to the linker; taking their address gives a
 * stable relocatable address resolvable at link time, so the table
 * entries below remain valid static initializers. */
extern uint16_t _currentSeasonShowing;
extern uint16_t _stayingInOldClub;
extern uint16_t _overdraftStreak;
extern uint16_t _overdraftWarningState;
extern uint16_t _performanceWarningState;
extern uint16_t _overdraftWarningCooldown;
extern uint16_t _performanceWarningCooldown;
extern uint16_t _youreSacked;
extern uint16_t _retired;
extern uint16_t _competitionSlotUpdatedFlag;
extern uint16_t _careerTeamLostMatch;
extern uint16_t _clubBusinessActive;
extern int32_t  _newBalance;
extern int32_t  _old_balance;
extern int32_t  _playerPurchases;
extern int32_t  _playerSales;
extern int32_t  _generalRunningCosts;
extern int32_t  _playerWagesBill;
extern int32_t  _gateReceipts;
extern int32_t  _startingBalance;
extern int32_t  _startingBalanceCopy;
extern int32_t  _competitionBonuses;
extern int32_t  _sensisoftPlcSponsorship;
extern uint16_t _financeTotalsNeedResetFlag;
extern uint32_t _weeklyRunningCosts;
extern uint32_t _weeklyPlayerWages;
extern int32_t  _totalTeamValue;
extern uint8_t  _weeklyWageByPlayerIndex[];
extern uint8_t  _playerFormModifiers[];
extern uint16_t _sensisoftPlcSponsorshipFactor;
extern uint16_t _additionalInvestments;
extern uint16_t _teamValueDivisor;
extern uint16_t _expectedLeagueRankIndex;
extern uint16_t _boardConfidenceHistory[4];
extern uint16_t _boardConfidenceScore;
extern uint16_t _previousBoardConfidence;
extern uint16_t _playWinCupAnimation;
extern uint16_t _minorTrophyBonusActive;
extern uint16_t _leagueTitleBonusActive;
extern uint16_t _majorTrophyBonusActive;
extern uint16_t _cupBonus1Active;
extern uint16_t _cupBonus2Active;
extern uint16_t _wonWorldCup;
extern uint16_t _wonContinentalCup;
extern uint16_t _wonInvitationTournament;
extern uint16_t _qualifyingStage1PendingFlag0;
extern uint16_t _qualifyingStage1PendingFlag1;
extern uint16_t _qualifyingStage2PendingFlag0;
extern uint16_t _qualifyingStage2PendingFlag1;
extern uint16_t _isCareer;
extern uint16_t _careerMatchSimulatedFlag;
extern uint16_t _gamesPlayed;
extern uint16_t _numWins;
extern uint16_t _numDrawn;
extern uint16_t _numLost;
extern uint16_t _goalsScored;
extern uint16_t _goalsTaken;
extern uint16_t _boardConfidenceMod0;
extern uint16_t _boardConfidenceMod1;
extern uint16_t _boardConfidenceMod2;
extern uint16_t _boardConfidenceMod3;
extern uint16_t _boardConfidenceValue0;
extern uint16_t _boardConfidenceValue1;
extern uint16_t _boardConfidenceMod4;
extern uint16_t _boardConfidenceMod5;
extern uint16_t _boardConfidenceMod6;
extern uint16_t _boardConfidenceMod7;
extern uint16_t _boardConfidenceMod8;
extern uint16_t _boardConfidenceMod9;
extern uint16_t _contractNegotiationActive;
extern uint16_t _wageScalingDivisor;
extern uint16_t _teamNoOfferAccepted;
extern int32_t  _teamFundsAccepted;
extern uint16_t _isNationalSelector;
extern uint8_t  _someTeamFlags;
extern uint16_t _lastNationalityCall;
extern uint8_t  _nationalTeamNationality;
extern uint8_t  _nationalTeamFileId;
extern uint16_t _sameNationalCallFlag;
extern uint16_t _sackedReputationScore;
extern uint16_t _nationalSelectorReputationPoints;
extern uint8_t  _nationalTeamFileCode;
extern uint16_t _seasonReview;
extern uint16_t _worldCupHostTeam;
extern uint16_t _worldCupQualifyingTeam;
extern uint16_t _euroChampionshipQualTeam;
extern uint16_t _selTeamNumber;
extern uint16_t _coachOrPlayer;
extern uint16_t _careerTeamLeague;
extern uint16_t _currentDivisionIndex;
extern uint16_t _leagueFirstRoundNumber;
extern uint16_t _playPromotedAnimation;
extern uint16_t _playRelegatedAnimation;
extern uint16_t _mrOrMs;
extern uint8_t  _managerSexBuffer[];
extern uint8_t  _managerNameBuffer[];
extern uint8_t  _managerSurnameBuffer[];
extern uint8_t  _plNationality;
extern uint16_t _winCupBonusCount;
extern uint16_t _minorTrophyBonusCount;
extern uint16_t _majorTrophyBonusCount;
extern int16_t  _careerReputationPoints;
extern uint16_t _removedOfferPlayersFromSelectedTeamCount;
extern uint16_t _removedOfferPlayersFromOtherTeamsCount;
extern uint16_t _homeMarketUpdateBacklog;
extern uint16_t _foreignMarketUpdateBacklog;
extern uint16_t _offerExpiryIterationsRemaining;
extern uint16_t _numForeignMarketPlayers;
extern uint16_t _numHomeMarketPlayers;
extern uint16_t _numOffers;
extern uint16_t _careerSeasonsCompleted;
extern uint16_t _numForeignMarketPlayers2;
extern uint16_t _pendingForeignPlayerCount;
extern uint16_t _seasonTransferQuota;
extern uint16_t _transferPoolSlotLimit;
extern uint16_t _offerSwapSlotLimit;
extern uint16_t _transferPoolSlotsUsed;
extern uint16_t _offerSwapSlotsUsed;
extern uint16_t _numNewJobOffers;
extern uint16_t _numJobOffers;
extern uint16_t _timeToNegotiate;
extern uint16_t _numPlayersOnTransferMarket;
extern uint16_t _lastOfferPrimaryPlayerOrdinal;
extern uint16_t _lastOfferExtraPlayersWanted;
extern uint16_t _lastOfferSwapPlayerOrdinal;
extern uint16_t _lastOfferExtraPlayerOrdinal;
extern uint16_t _offerNegotiationAttemptCount;
extern uint16_t _transferMarketRandOffset0;
extern uint16_t _transferMarketRandOffset1;
extern uint16_t _transferMarketRandOffset2;
extern uint16_t _transferMarketPhase;
extern uint16_t _careerNumPlayers;
extern uint16_t _selTeamSelectedPlayerPosition;
extern uint8_t  _lastFinancialWeekIndex;
extern uint16_t _numPlayers;
extern uint16_t _currentTeamNumber;
extern uint16_t _currentTeamSelectedPlayerPosition;
extern uint16_t _trainingMatchSavedPlayerPos;
extern uint16_t _lastMatchSavedPlayerPos;
extern uint16_t _lastMatchSavedCoachOrPlayer;
extern uint8_t  _currentTactics;
extern uint8_t  _lastMatchSavedTeamControls;
extern uint16_t _seasonFinished;
extern uint16_t _careerFinished;
extern uint16_t _euroCupSlotIndex;
extern uint16_t _seasonNumberOfSquads;
extern uint16_t _teamSelectionKeyCacheIndex;
extern uint16_t _teamSelectionKeyCacheOffset;
extern uint16_t _teamSelectionKeyCacheCount;
extern uint16_t _careerSlotProcessingInProgress;
extern uint16_t _teamSelectionKeyCacheEnd;
extern uint8_t  _careerJobOfferPhaseBias;     /* promoted from cache-blob byte 1 */
/* primaryComp* slot-0 scalars (competitionOver, diyPresetNameOfCompetition,
 * majorTrophyBonusValue, numDiyTeams, primaryCompWinnerTeam, primaryCompSquadViewParam,
 * primaryCompSlotStateA-C, diyTeamMinDivisionIndex, diyTeamMaxDivisionOffset,
 * leaguePromotionCount, leagueRelegationCount, leagueWarningsEnabled) migrated to
 * primaryCompetitionBuf.tourn.header.* (and .tourn.leagueWarningsEnabled) -- slot-0
 * untangle, S6. Aliases retired. */
extern uint8_t  _viewRandomPhase;
extern uint8_t  _seasonPlaying;
extern uint16_t _inFriendlyMenu;
extern uint16_t _isNationalTeam;
extern uint16_t _gameType;
extern uint16_t _contestStarted;
extern uint16_t _careerLoadedFlag;
extern uint16_t _gameLength;
extern uint16_t _autoReplays;
extern uint16_t _menuMusic;
extern uint16_t _autoSaveHighlights;
extern uint16_t _allPlayerTeamsEqual;
extern uint16_t _pitchType;
extern uint8_t  _seed2;
extern uint8_t  _randXorIndex2;
extern uint8_t  _randXorKey2;
extern uint8_t  _newRandomSeedCopy;
extern uint16_t _randomSeed;
extern uint16_t _saveFileVersion;
extern uint32_t _saveFileAmigaType;
/* numSelectedTeams already declared at file top. */

/* secondaryComp* (name/minorTrophyBonusValue/winnerTeam/slotStateA-C/finished) migrated to
 * competitionContextBuf.tourn.header.* (slot-1 untangle, S4) */
/* tertiaryComp* (name/leagueTitleBonusValue/winnerTeam/slotStateA-C/finished) migrated to
 * tertiaryCompetitionContextBuf.tourn.header.* (slot-2 untangle, S3) */
/* diyCompSlotStateA/B/C migrated to diyCompetitionContextBuf.tourn.header.slotState* (slot-4 untangle, S2) */
/* quaternaryComp* (name/majorTrophyOrCupBonusValue/winnerTeam/slotStateA-C) migrated to
 * euroCupsCompetitionContextBuf.tourn.header.* (slot-3 untangle, S5) */
extern uint16_t _removedPlayerStatAccum;
extern uint8_t  _careerUnavailablePlayerList[];
extern uint8_t  _currentMatchPlayers[];
extern uint8_t  _seasonSquadTeamNumbers[];
extern uint8_t  _playerPositions[];
extern uint8_t  _trainingMatchSavedPlayers[];
extern uint8_t  _trainingMatchSavedShirtNums[];
extern uint8_t  _trainingMatchSavedTactics[];
extern uint8_t  _lastMatchPlayers[];
extern uint8_t  _lastMatchSavedShirtNums[];
extern uint8_t  _careerLastMatchIndexByCompetitionSlot[256];
extern uint8_t  _teamBudgetRankTable[];
extern uint8_t  _playersOnTransferMarket[];
extern uint8_t  _foreignMarketPlayers[];
extern uint8_t  _homeMarketPlayers[];
extern uint8_t  _transferOfferTable[];
extern uint8_t  _newJobOffers[];
extern uint8_t  _jobOffers[];

/* careerCompetitionSlotPtrTable entry buffers — declared further down
 * for the named-blob section but needed earlier for the per-slot type
 * scalars at the top of the s_career_scalars table. */
extern CompetitionSlotBuffer _primaryCompetitionBuf;          /* slot-0 untangle (S6): typed buffer */
extern CompetitionSlotBuffer _competitionContextBuf;          /* slot-1 untangle (S4): typed buffer */
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;   /* slot-2 untangle (S3): typed buffer */
extern CompetitionSlotBuffer _euroCupsCompetitionContextBuf;   /* slot-3 untangle (S5): typed buffer */
extern CompetitionSlotBuffer _diyCompetitionContextBuf;   /* slot-4 untangle (S2): typed buffer */

/* Other career-overlay byte regions referenced by the scalar table —
 * declared further down for the per-region section but needed earlier
 * for static initialiser address resolution. */
extern uint8_t  _diyLeagueSeasonStateTable[GLOBAL_TEAM_NUMBERS];
/* diyTeamIdListType0/1/2 reached via primaryCompetitionBuf casts now (S6). */
extern uint8_t  _careerUnavailablePlayerLookupTable[];
extern uint8_t  _careerUnavailablePlayerNames[];
extern uint8_t  _teamSelectionKeyCache[];
extern uint8_t  _teamTacticsUSERA[];
extern uint8_t  _teamTacticsUSERB[];
extern uint8_t  _teamTacticsUSERC[];
extern uint8_t  _teamTacticsUSERD[];
extern uint8_t  _teamTacticsUSERE[];
extern uint8_t  _teamTacticsUSERF[];
extern uint8_t  _euroCupCopy[];

/* Career meta / flags / finance / market state.
 * Address-based: each entry takes &symbol so the table is
 * resilient to overlay layout shifts (e.g. eventual 64-bit move)
 * and self-documenting. The compile-time address-constant rule
 * applies — `&overdraftStreak` is valid in a static initializer. */
#define FX(name, sym, type) { name, (void *)&sym, 0, type, 0 }
#define FA(name, sym, len, type) { name, (void *)&sym, len, type, 0 }
/* FXC/FAC — same as FX/FA but tagged comp=1: the entry belongs to the
 * .DIY/.PRE/.SEA competition-slot save set. Membership is the PRIMARY
 * competition slot + save-file header + team/tmd counts (the only state a
 * standalone competition file carries). Tagging by identity replaces the old
 * field_in_range(&primaryCompetitionBuf, &numSelectedTeams) address span,
 * which was forbidden (a build/target-specific symbol-order dependency) and,
 * after the S6 slot-0 untangle moved primaryCompetitionBuf 1.7 MB away from
 * numSelectedTeams, had ballooned to sweep unrelated career globals. */
#define FXC(name, sym, type) { name, (void *)&sym, 0, type, 1 }
#define FAC(name, sym, len, type) { name, (void *)&sym, len, type, 1 }
/* FXS/FAS — comp=2: in the competition save set, but not pre-zeroed on load.
 * See the `comp` field's comment on FieldDesc. */
#define FXS(name, sym, type) { name, (void *)&sym, 0, type, 2 }
#define FAS(name, sym, len, type) { name, (void *)&sym, len, type, 2 }

/* Compile-time invariants for the primary-competition header runs (see
 * the primary_comp_ids/dims/months/state/ex/matchdata_* entries below).
 * They assert the run boundaries the table hard-codes: each typed run
 * starts exactly where the previous ends (except the deliberately-skipped
 * 64-bit trailing pad before run E), and the final run reaches
 * diyTeamIdListType2. If the DIYCompetition / DIYCompetitionEx layout
 * drifts, these fire at compile time instead of silently mis-saving the
 * primary competition. Same typedef-char trick game_types.h's
 * size_check[] asserts use, so the Amiga back-port compiles identically. */
typedef char _primary_name_ptr_at_0x28[
    (offsetof(DIYCompetition, namePtr) == 0x28) ? 1 : -1];
/* Run A (6 u8 from compId) ends at numTeams (run B). */
typedef char _primary_run_a_to_b[
    (offsetof(DIYCompetition, compSlot) + 6 == offsetof(DIYCompetition, numTeams)) ? 1 : -1];
/* Run B (3 int16 from numTeams) ends at startMonth (run C). */
typedef char _primary_run_b_to_c[
    (offsetof(DIYCompetition, numTeams) + 6 == offsetof(DIYCompetition, startMonth)) ? 1 : -1];
/* Run C (2 int8 from startMonth) ends at resultCode (run D). */
typedef char _primary_run_c_to_d[
    (offsetof(DIYCompetition, startMonth) + 2 == offsetof(DIYCompetition, resultCode)) ? 1 : -1];
/* Run D (19 int16 from resultCode) ends right past playEachTeam, the last
 * DIYCompetition field. 38 bytes = 0x26. */
typedef char _primary_run_d_end[
    (offsetof(DIYCompetition, resultCode) + 38
        == offsetof(DIYCompetition, playEachTeam) + 2) ? 1 : -1];
/* Run E (2 uint16) starts at the Ex fields — after the 64-bit trailing
 * pad — and ends at matchData (run F). */
typedef char _primary_run_e_to_f[
    (offsetof(DIYCompetitionEx, minSubstitutes) + 4
        == offsetof(DIYCompetitionEx, matchData)) ? 1 : -1];
/* Runs F (38 bytes uint16) + G (16 bytes) reach diyTeamIdListType2:
 * matchData + 0x26 + 16 == header end. With the overlay's SECOND +4 pad
 * in place (see swos_globals_career_overlay.c), the matchData→header-end
 * window is a uniform 54 bytes (0x36) on BOTH targets — only the absolute
 * end differs: 0x9A on 32-bit, 0xA2 on 64-bit. Asserting against the
 * struct keeps this honest if DIYCompetitionEx ever shifts again. */
#if UINTPTR_MAX == 0xFFFFFFFFu
typedef char _primary_run_g_reaches_team_ids[
    (offsetof(DIYCompetitionEx, matchData) + 0x26 + 16 == 0x9A) ? 1 : -1];
#else
typedef char _primary_run_g_reaches_team_ids[
    (offsetof(DIYCompetitionEx, matchData) + 0x26 + 16 == 0xA2) ? 1 : -1];
#endif

/* SLOT_ALIAS untangle S6 (D-A1): pin the two cast-through BE team-id descriptor
 * addresses to the retired aliases' offsets on BOTH arches, so a wrong typed path
 * is caught at compile time (the old aliases were primaryCompetitionBuf + 0x12E /
 * 0x24E + 2*CFB_NP; CFB_NP = sizeof(void*)-4). type2 (tourn.teamIds@0x9A) and
 * leagueWarningsEnabled@sizeof(DIYCompetition) are already pinned in game_types.h. */
_Static_assert(offsetof(DIYCompetitionEx, matchData.league.leagueTeamIds)
               == 0x12E + 2 * (sizeof(void *) - 4), "S6 D-A1: diy_team_id_list_type_0 byte-identity");
_Static_assert(offsetof(DIYCompetitionEx, cupTeamIds)
               == 0x24E + 2 * (sizeof(void *) - 4), "S6 D-A1: diy_team_id_list_type_1 byte-identity");

static const FieldDesc s_career_scalars[] = {
    /* tmd_team_count — uint16 at &careerFileBuffer + 0, holds the count
     * of populated TeamFileEx records in the TMD pool that follows at
     * tmdFileBuffer (= &careerFileBuffer + 2). Consumed by
     * SearchTeamInTmdFile and friends in career_teams.c. */
    FXC("tmd_team_count",                 _careerFileBuffer,            FLD_U16),

    /* Per-slot type bytes — first int16 of each careerCompetitionSlotPtrTable
     * entry (= DIYCompetition.competitionType field). Saved explicitly so
     * Career_LoadCompetitionSlotStateFromBuffer dispatches to the right
     * branch on load. Previously this was inferred from currentCompetitionPtr
     * (e9a308b) which broke when the saved in-memory state disagreed with
     * the pointer; an explicit field is authoritative and survives any
     * future struct reshuffles. */
    FXC("primary_slot_type",              _primaryCompetitionBuf.tourn.header.competitionType, FLD_S16),
    FX("secondary_slot_type",             _competitionContextBuf.tourn.header.competitionType, FLD_S16),
    FX("tertiary_slot_type",              _tertiaryCompetitionContextBuf.tourn.header.competitionType, FLD_S16),
    FX("quaternary_slot_type",            _euroCupsCompetitionContextBuf.tourn.header.competitionType, FLD_S16),
    FX("diy_slot_type",                   _diyCompetitionContextBuf.tourn.header.competitionType, FLD_S16),

    FX("current_season_showing",          _currentSeasonShowing,        FLD_U16),
    FX("staying_in_old_club",             _stayingInOldClub,            FLD_U16),
    FX("overdraft_streak",                _overdraftStreak,             FLD_U16),
    FX("overdraft_warning_state",         _overdraftWarningState,       FLD_U16),
    FX("performance_warning_state",       _performanceWarningState,     FLD_U16),
    FX("overdraft_warning_cooldown",      _overdraftWarningCooldown,    FLD_U16),
    FX("performance_warning_cooldown",    _performanceWarningCooldown,  FLD_U16),
    FX("youre_sacked",                    _youreSacked,                 FLD_U16),
    FX("retired",                         _retired,                     FLD_U16),
    FX("competition_slot_updated_flag",   _competitionSlotUpdatedFlag,  FLD_U16),
    FX("career_team_lost_match",          _careerTeamLostMatch,         FLD_U16),
    FX("club_business_active",            _clubBusinessActive,          FLD_U16),
    FX("new_balance",                     _newBalance,                  FLD_S32),
    FX("old_balance",                     _old_balance,                 FLD_S32),
    FX("player_purchases",                _playerPurchases,             FLD_S32),
    FX("player_sales",                    _playerSales,                 FLD_S32),
    FX("general_running_costs",           _generalRunningCosts,         FLD_S32),
    FX("player_wages_bill",               _playerWagesBill,             FLD_S32),
    FX("gate_receipts",                   _gateReceipts,                FLD_S32),
    FX("starting_balance",                _startingBalance,             FLD_S32),
    FX("starting_balance_copy",           _startingBalanceCopy,         FLD_S32),
    FX("competition_bonuses",             _competitionBonuses,          FLD_S32),
    FX("sensisoft_plc_sponsorship",       _sensisoftPlcSponsorship,     FLD_S32),
    FX("finance_totals_need_reset_flag",  _financeTotalsNeedResetFlag,  FLD_U16),
    FX("weekly_running_costs",            _weeklyRunningCosts,          FLD_U32),
    FX("weekly_player_wages",             _weeklyPlayerWages,           FLD_U32),
    FX("total_team_value",                _totalTeamValue,              FLD_S32),
    /* int32_t[32] in game code (career_status.c) — NOT bytes. FLD_BYTES
     * byte-swapped each wage cross-target (64-bit LE save → BE Amiga),
     * summing to garbage → totalTeamValue overflow → gate-receipts panic.
     * Saved as endian-neutral 32-bit decimals. (Live-gdb-confirmed bug.) */
    FA("weekly_wage_by_player_index",     _weeklyWageByPlayerIndex, 128,FLD_U32_ARRAY),
    FA("player_form_modifiers",           _playerFormModifiers,     32, FLD_BYTES),
    FX("sensisoft_plc_sponsorship_factor",_sensisoftPlcSponsorshipFactor,FLD_U16),
    FX("additional_investments",          _additionalInvestments,       FLD_U16),
    FX("team_value_divisor",              _teamValueDivisor,            FLD_U16),
    FX("expected_league_rank_index",      _expectedLeagueRankIndex,     FLD_U16),
    /* int16_t[4] in game code — endian-neutral 16-bit decimals, not bytes. */
    FA("board_confidence_history",        _boardConfidenceHistory, 8,  FLD_U16_ARRAY),
    FX("board_confidence_score",          _boardConfidenceScore,        FLD_U16),
    FX("previous_board_confidence",       _previousBoardConfidence,     FLD_U16),
    FX("play_win_cup_animation",          _playWinCupAnimation,         FLD_U16),
    FX("minor_trophy_bonus_active",       _minorTrophyBonusActive,      FLD_U16),
    FX("league_title_bonus_active",       _leagueTitleBonusActive,      FLD_U16),
    FX("major_trophy_bonus_active",       _majorTrophyBonusActive,      FLD_U16),
    FX("cup_bonus_1_active",              _cupBonus1Active,             FLD_U16),
    FX("cup_bonus_2_active",              _cupBonus2Active,             FLD_U16),
    FX("won_world_cup",                   _wonWorldCup,                 FLD_U16),
    FX("won_continental_cup",             _wonContinentalCup,           FLD_U16),
    FX("won_invitation_tournament",       _wonInvitationTournament,     FLD_U16),
    FX("qualifying_stage_1_pending_flag0",_qualifyingStage1PendingFlag0,FLD_U16),
    FX("qualifying_stage_1_pending_flag1",_qualifyingStage1PendingFlag1,FLD_U16),
    FX("qualifying_stage_2_pending_flag0",_qualifyingStage2PendingFlag0,FLD_U16),
    FX("qualifying_stage_2_pending_flag1",_qualifyingStage2PendingFlag1,FLD_U16),
    FX("is_career",                       _isCareer,                    FLD_U16),
    FX("career_match_simulated_flag",     _careerMatchSimulatedFlag,    FLD_U16),
    FX("games_played",                    _gamesPlayed,                 FLD_U16),
    FX("num_wins",                        _numWins,                     FLD_U16),
    FX("num_drawn",                       _numDrawn,                    FLD_U16),
    FX("num_lost",                        _numLost,                     FLD_U16),
    FX("goals_scored",                    _goalsScored,                 FLD_U16),
    FX("goals_taken",                     _goalsTaken,                  FLD_U16),
    /* Board-confidence mods (10 x uint16) are INTERLEAVED with value_0/1 in BSS
     * (Mod0-3, Value0, Value1, Mod4-9 — see career_board_confidence.c). Serialize
     * each by NAME, in struct order. The old single FA(...Mod0, 20, FLD_U16_ARRAY)
     * span conflated Value0/1 into the "mods" array AND dropped Mod8/Mod9 (past the
     * 20-byte/10-u16 window) so they were never saved; it also overlapped the
     * value_0/1 FX, which the round-trip verify flagged as a false mismatch.
     * Endian-neutral 16-bit decimals. */
    FX("board_confidence_mod_0",          _boardConfidenceMod0,         FLD_U16),
    FX("board_confidence_mod_1",          _boardConfidenceMod1,         FLD_U16),
    FX("board_confidence_mod_2",          _boardConfidenceMod2,         FLD_U16),
    FX("board_confidence_mod_3",          _boardConfidenceMod3,         FLD_U16),
    FX("board_confidence_value_0",        _boardConfidenceValue0,       FLD_U16),
    FX("board_confidence_value_1",        _boardConfidenceValue1,       FLD_U16),
    FX("board_confidence_mod_4",          _boardConfidenceMod4,         FLD_U16),
    FX("board_confidence_mod_5",          _boardConfidenceMod5,         FLD_U16),
    FX("board_confidence_mod_6",          _boardConfidenceMod6,         FLD_U16),
    FX("board_confidence_mod_7",          _boardConfidenceMod7,         FLD_U16),
    FX("board_confidence_mod_8",          _boardConfidenceMod8,         FLD_U16),
    FX("board_confidence_mod_9",          _boardConfidenceMod9,         FLD_U16),
    FX("contract_negotiation_active",     _contractNegotiationActive,   FLD_U16),
    FX("wage_scaling_divisor",            _wageScalingDivisor,          FLD_U16),
    FX("team_no_offer_accepted",          _teamNoOfferAccepted,         FLD_U16),
    FX("team_funds_accepted",             _teamFundsAccepted,           FLD_S32),
    FX("is_national_selector",            _isNationalSelector,          FLD_U16),
    FX("some_team_flags",                 _someTeamFlags,               FLD_U8 ),
    FX("last_nationality_call",           _lastNationalityCall,         FLD_U16),
    FX("national_team_nationality",       _nationalTeamNationality,     FLD_U8 ),
    FX("national_team_file_id",           _nationalTeamFileId,          FLD_U8 ),
    FX("same_national_call_flag",         _sameNationalCallFlag,        FLD_U16),
    FX("sacked_reputation_score",         _sackedReputationScore,       FLD_U16),
    FX("national_selector_reputation_points",_nationalSelectorReputationPoints,FLD_U16),
    FX("national_team_file_code",         _nationalTeamFileCode,        FLD_U8 ),
    FX("season_review",                   _seasonReview,                FLD_U16),
    FX("world_cup_host_team",             _worldCupHostTeam,            FLD_U16),
    FX("world_cup_qualifying_team",       _worldCupQualifyingTeam,      FLD_U16),
    FX("euro_championship_qual_team",     _euroChampionshipQualTeam,    FLD_U16),
    FX("sel_team_number",                 _selTeamNumber,               FLD_U16),
    FX("coach_or_player",                 _coachOrPlayer,               FLD_U16),
    FX("career_team_league",              _careerTeamLeague,            FLD_U16),
    FX("current_division_index",          _currentDivisionIndex,        FLD_U16),
    FX("league_first_round_number",       _leagueFirstRoundNumber,      FLD_U16),
    FX("play_promoted_animation",         _playPromotedAnimation,       FLD_U16),
    FX("play_relegated_animation",        _playRelegatedAnimation,      FLD_U16),
    FX("mr_or_ms",                        _mrOrMs,                      FLD_U16),
    FA("manager_sex_buffer",              _managerSexBuffer, 4,         FLD_STR ),
    FA("manager_name_buffer",             _managerNameBuffer, 9,        FLD_STR ),
    FA("manager_surname_buffer",          _managerSurnameBuffer, 13,    FLD_STR ),
    FX("pl_nationality",                  _plNationality,               FLD_U8 ),
    FX("win_cup_bonus_count",             _winCupBonusCount,            FLD_U16),
    FX("minor_trophy_bonus_count",        _minorTrophyBonusCount,       FLD_U16),
    FX("major_trophy_bonus_count",        _majorTrophyBonusCount,       FLD_U16),
    FX("career_reputation_points",        _careerReputationPoints,      FLD_S16),
    FX("removed_offer_players_from_selected_team_count",_removedOfferPlayersFromSelectedTeamCount,FLD_U16),
    FX("removed_offer_players_from_other_teams_count",  _removedOfferPlayersFromOtherTeamsCount,  FLD_U16),
    FX("home_market_update_backlog",      _homeMarketUpdateBacklog,     FLD_U16),
    FX("foreign_market_update_backlog",   _foreignMarketUpdateBacklog,  FLD_U16),
    FX("offer_expiry_iterations_remaining",_offerExpiryIterationsRemaining,FLD_U16),
    FX("num_foreign_market_players",      _numForeignMarketPlayers,     FLD_U16),
    FX("num_home_market_players",         _numHomeMarketPlayers,        FLD_U16),
    FX("num_offers",                      _numOffers,                   FLD_U16),
    FX("career_seasons_completed",        _careerSeasonsCompleted,      FLD_U16),
    FX("num_foreign_market_players_2",    _numForeignMarketPlayers2,    FLD_U16),
    FX("pending_foreign_player_count",    _pendingForeignPlayerCount,   FLD_U16),
    FX("season_transfer_quota",           _seasonTransferQuota,         FLD_U16),
    FX("transfer_pool_slot_limit",        _transferPoolSlotLimit,       FLD_U16),
    FX("offer_swap_slot_limit",           _offerSwapSlotLimit,          FLD_U16),
    FX("transfer_pool_slots_used",        _transferPoolSlotsUsed,       FLD_U16),
    FX("offer_swap_slots_used",           _offerSwapSlotsUsed,          FLD_U16),
    FX("num_new_job_offers",              _numNewJobOffers,             FLD_U16),
    FX("num_job_offers",                  _numJobOffers,                FLD_U16),
    FX("time_to_negotiate",               _timeToNegotiate,             FLD_U16),
    FX("num_players_on_transfer_market",  _numPlayersOnTransferMarket,  FLD_U16),
    FX("last_offer_primary_player_ordinal",_lastOfferPrimaryPlayerOrdinal,FLD_U16),
    FX("last_offer_extra_players_wanted", _lastOfferExtraPlayersWanted, FLD_U16),
    FX("last_offer_swap_player_ordinal",  _lastOfferSwapPlayerOrdinal,  FLD_U16),
    FX("last_offer_extra_player_ordinal", _lastOfferExtraPlayerOrdinal, FLD_U16),
    FX("offer_negotiation_attempt_count", _offerNegotiationAttemptCount,FLD_U16),
    FX("transfer_market_rand_offset_0",   _transferMarketRandOffset0,   FLD_U16),
    FX("transfer_market_rand_offset_1",   _transferMarketRandOffset1,   FLD_U16),
    FX("transfer_market_rand_offset_2",   _transferMarketRandOffset2,   FLD_U16),
    FX("transfer_market_phase",           _transferMarketPhase,         FLD_U16),
    FX("career_num_players",              _careerNumPlayers,            FLD_U16),
    FX("sel_team_selected_player_position",_selTeamSelectedPlayerPosition,FLD_U16),
    FX("last_financial_week_index",       _lastFinancialWeekIndex,      FLD_U8 ),
    FX("num_players",                     _numPlayers,                  FLD_U16),
    FX("current_team_number",             _currentTeamNumber,           FLD_U16),
    FX("current_team_selected_player_position",_currentTeamSelectedPlayerPosition,FLD_U16),
    FX("training_match_saved_player_pos", _trainingMatchSavedPlayerPos, FLD_U16),
    FX("last_match_saved_player_pos",     _lastMatchSavedPlayerPos,     FLD_U16),
    FX("last_match_saved_coach_or_player",_lastMatchSavedCoachOrPlayer, FLD_U16),
    FX("current_tactics",                 _currentTactics,              FLD_U8 ),
    FX("last_match_saved_team_controls",  _lastMatchSavedTeamControls,  FLD_U8 ),
    FX("season_finished",                 _seasonFinished,              FLD_U16),
    FX("career_finished",                 _careerFinished,              FLD_U16),
    FX("euro_cup_slot_index",             _euroCupSlotIndex,            FLD_U16),
    FX("season_number_of_squads",         _seasonNumberOfSquads,        FLD_U16),
    FX("team_selection_key_cache_index",  _teamSelectionKeyCacheIndex,  FLD_U16),
    FX("team_selection_key_cache_offset", _teamSelectionKeyCacheOffset, FLD_U16),
    FX("team_selection_key_cache_count",  _teamSelectionKeyCacheCount,  FLD_U16),
    FX("career_slot_processing_in_progress",_careerSlotProcessingInProgress,FLD_U16),
    FX("team_selection_key_cache_end",    _teamSelectionKeyCacheEnd,    FLD_U16),
    /* Promoted out of the cache blob (was teamSelectionKeyCache byte 1, the
     * Amiga cache/phase-bias overlap). Old saves lack this key -> loads as 0,
     * re-derived in InitCareer. See project_keycache_cluster_design. */
    FX("career_job_offer_phase_bias",     _careerJobOfferPhaseBias,     FLD_U8 ),
    /* Slot-0 (primary) header scalars — SLOT_ALIAS untangle S6. Same addresses as
     * the retired competitionOver/diyPresetNameOfCompetition/.../leagueRelegationCount
     * aliases (typed primaryCompetitionBuf.tourn.header.* paths), so byte-identical.
     * major_trophy_bonus_value keeps FLD_U16 over the .trophyBonusValue byte@0x30
     * (reads 0x30-0x31, the legacy alias width — S4 precedent, do not "fix"). */
    FXC("competition_over",               _primaryCompetitionBuf.tourn.header.competitionOver, FLD_U16),
    FAC("diy_preset_name_of_competition", _primaryCompetitionBuf.tourn.header.competitionName, 36, FLD_STR),
    FXC("major_trophy_bonus_value",       _primaryCompetitionBuf.tourn.header.trophyBonusValue, FLD_U16),
    FXC("num_diy_teams",                  _primaryCompetitionBuf.tourn.header.numTeams, FLD_U16),
    FXC("primary_comp_winner_team",       _primaryCompetitionBuf.tourn.header.winnerTeam, FLD_U16),
    FXC("primary_comp_squad_view_param",  _primaryCompetitionBuf.tourn.header.squadViewParam, FLD_U16),
    FXC("primary_comp_slot_state_a",      _primaryCompetitionBuf.tourn.header.slotStateA, FLD_U16),
    FXC("primary_comp_slot_state_b",      _primaryCompetitionBuf.tourn.header.slotStateB, FLD_U16),
    FXC("primary_comp_slot_state_c",      _primaryCompetitionBuf.tourn.header.slotStateC, FLD_U16),
    FXC("diy_team_min_division_index",    _primaryCompetitionBuf.tourn.header.minDivisionIndex, FLD_U16),
    FXC("diy_team_max_division_offset",   _primaryCompetitionBuf.tourn.header.maxDivisionOffset, FLD_U16),
    FXC("league_promotion_count",         _primaryCompetitionBuf.tourn.header.promotionCount, FLD_U16),
    FXC("league_relegation_count",        _primaryCompetitionBuf.tourn.header.relegationCount, FLD_U16),
    FXC("league_warnings_enabled",        _primaryCompetitionBuf.tourn.leagueWarningsEnabled, FLD_U16),
    FX("view_random_phase",               _viewRandomPhase,             FLD_U8 ),
    FX("season_playing",                  _seasonPlaying,               FLD_U8 ),
    FX("in_friendly_menu",                _inFriendlyMenu,              FLD_U16),
    FX("is_national_team",                _isNationalTeam,              FLD_U16),
    FX("game_type",                       _gameType,                    FLD_U16),
    FX("contest_started",                 _contestStarted,              FLD_U16),
    FX("career_loaded_flag",              _careerLoadedFlag,            FLD_U16),
    /* The six SaveOptions/RestoreOptions words. In the original these sat inside
     * the .PRE/.DIY dump region ([competitionFileBuffer, numSelectedTeams)), and
     * LoadDIYFile/LoadPresetFile only call RestoreOptions on the ERROR path
     * (SWOS2.s:125758) — a successful load deliberately adopts the file's
     * options, which is also why it follows up with CycleMenuMusic. Carried in
     * the competition save so that stays true. */
    FXS("game_length",                    _gameLength,                  FLD_U16),
    FXS("auto_replays",                   _autoReplays,                 FLD_U16),
    FXS("menu_music",                     _menuMusic,                   FLD_U16),
    FXS("auto_save_highlights",           _autoSaveHighlights,          FLD_U16),
    FXS("all_player_teams_equal",         _allPlayerTeamsEqual,         FLD_U16),
    FXS("pitch_type",                     _pitchType,                   FLD_U16),
    FX("seed_2",                          _seed2,                       FLD_U8 ),
    FX("rand_xor_index_2",                _randXorIndex2,               FLD_U8 ),
    FX("rand_xor_key_2",                  _randXorKey2,                 FLD_U8 ),
    FX("new_random_seed_copy",            _newRandomSeedCopy,           FLD_U8 ),
    FX("random_seed",                     _randomSeed,                  FLD_U16),
    FXC("save_file_version",              _saveFileVersion,             FLD_U16),
    FXC("save_file_amiga_type",           _saveFileAmigaType,           FLD_U32),
    FXC("num_selected_teams_inner",       _numSelectedTeams,            FLD_U16),

    /* Comp context names + slot states (secondary/tertiary/quaternary). */
    FA("secondary_competition_name",      _competitionContextBuf.tourn.header.competitionName, 36, FLD_STR),
    FX("minor_trophy_bonus_value",        _competitionContextBuf.tourn.header.trophyBonusValue, FLD_U16),
    FX("secondary_comp_winner_team",      _competitionContextBuf.tourn.header.winnerTeam, FLD_U16),
    FX("secondary_comp_slot_state_a",     _competitionContextBuf.tourn.header.slotStateA, FLD_U16),
    FX("secondary_comp_slot_state_b",     _competitionContextBuf.tourn.header.slotStateB, FLD_U16),
    FX("secondary_comp_slot_state_c",     _competitionContextBuf.tourn.header.slotStateC, FLD_U16),
    FX("secondary_comp_finished",         _competitionContextBuf.tourn.header.compFinished, FLD_U16),
    FA("tertiary_competition_name",       _tertiaryCompetitionContextBuf.tourn.header.competitionName, 36, FLD_STR),
    FX("league_title_bonus_value",        _tertiaryCompetitionContextBuf.tourn.header.trophyBonusValue, FLD_U16),
    FX("tertiary_comp_winner_team",       _tertiaryCompetitionContextBuf.tourn.header.winnerTeam, FLD_U16),
    FX("tertiary_comp_slot_state_a",      _tertiaryCompetitionContextBuf.tourn.header.slotStateA, FLD_U16),
    FX("tertiary_comp_slot_state_b",      _tertiaryCompetitionContextBuf.tourn.header.slotStateB, FLD_U16),
    FX("tertiary_comp_slot_state_c",      _tertiaryCompetitionContextBuf.tourn.header.slotStateC, FLD_U16),
    FX("tertiary_comp_finished",          _tertiaryCompetitionContextBuf.tourn.header.compFinished, FLD_U16),
    FX("diy_comp_slot_state_a",           _diyCompetitionContextBuf.tourn.header.slotStateA, FLD_U16),
    FX("diy_comp_slot_state_b",           _diyCompetitionContextBuf.tourn.header.slotStateB, FLD_U16),
    FX("diy_comp_slot_state_c",           _diyCompetitionContextBuf.tourn.header.slotStateC, FLD_U16),
    FA("quaternary_competition_name",     _euroCupsCompetitionContextBuf.tourn.header.competitionName, 36,FLD_STR),
    FX("major_trophy_or_cup_bonus_value", _euroCupsCompetitionContextBuf.tourn.header.trophyBonusValue, FLD_U16),
    FX("quaternary_comp_winner_team",     _euroCupsCompetitionContextBuf.tourn.header.winnerTeam, FLD_U16),
    FX("quaternary_comp_slot_state_a",    _euroCupsCompetitionContextBuf.tourn.header.slotStateA, FLD_U16),
    FX("quaternary_comp_slot_state_b",    _euroCupsCompetitionContextBuf.tourn.header.slotStateB, FLD_U16),
    FX("quaternary_comp_slot_state_c",    _euroCupsCompetitionContextBuf.tourn.header.slotStateC, FLD_U16),

    /* Small named arrays / packed records. */
    /* uint16_t in game code — endian-neutral 16-bit decimal. */
    FA("removed_player_stat_accum",       _removedPlayerStatAccum, 2,   FLD_U16_ARRAY),
    FA("career_unavailable_player_list",  _careerUnavailablePlayerList, 16, FLD_BYTES),
    FA("current_match_players",           _currentMatchPlayers, 32,     FLD_BYTES),
    /* uint16[24] packed team ids — endian-neutral words (was FLD_BYTES, which
     * byte-swapped these host-native uint16s on an LE<->BE cross-target load). */
    FA("season_squad_team_numbers",       _seasonSquadTeamNumbers, 48,  FLD_U16_ARRAY),
    FA("player_positions",                _playerPositions, 32,         FLD_BYTES),
    FA("training_match_saved_players",    _trainingMatchSavedPlayers, 32, FLD_BYTES),
    FA("training_match_saved_shirt_nums", _trainingMatchSavedShirtNums, 32, FLD_BYTES),
    FA("training_match_saved_tactics",    _trainingMatchSavedTactics, 2,FLD_BYTES),
    FA("last_match_players",              _lastMatchPlayers, 32,        FLD_BYTES),
    FA("last_match_saved_shirt_nums",     _lastMatchSavedShirtNums, 32, FLD_BYTES),
    FA("career_last_match_index_by_competition_slot",
                                          _careerLastMatchIndexByCompetitionSlot, 256, FLD_BYTES),
    FA("team_budget_rank_table",          _teamBudgetRankTable, 128,    FLD_BYTES),
    FA("players_on_transfer_market",      _playersOnTransferMarket, 10, FLD_BYTES),

    /* Market player slot tables — byte arrays of compact records. The
     * team-id fields are explicit BE byte pairs (id>>8 / id), so they are
     * endian-stable as raw bytes — safe as FLD_BYTES. */
    FA("foreign_market_players",          _foreignMarketPlayers, 60,    FLD_BYTES),
    FA("home_market_players",             _homeMarketPlayers, 100,      FLD_BYTES),
    /* transfer_offer_table / job_offers / new_job_offers were FLD_BYTES but
     * hold host-native multi-byte fields (uint16/uint32) that byte-swap on a
     * cross-target load. They are now emitted as typed arrays-of-objects in
     * write_career / load_career (s_transfer_offer_fields / s_job_offer_fields). */

    /* Per-globalTeamNumber league offset (signed). Indexed by team's
     * globalTeamNumber, read in teams.c:setLeagueNumbers and
     * career_init.c when deriving currentDivisionIndex. Editable via the
     * DIY-edit-leagues menu's scratch buffer. Flat int8[2000]. */
    FA("diy_league_season_state_table",   _diyLeagueSeasonStateTable, GLOBAL_TEAM_NUMBERS, FLD_BYTES),

    /* DIY team-id lists per competition type (league/cup/hybrid). Each
     * is an array of BE byte-pairs [country, ordinal] — see
     * FindTeamIdInDiyTeamsList in transfer_market.c. Saved as flat
     * bytes for round-trip fidelity. */
    /* SLOT_ALIAS untangle S6 (D-A1): the three legacy contiguous BE team-id views,
     * rebased onto primaryCompetitionBuf via the same casts the consumers use
     * (type0=matchData.league.leagueTeamIds@0x12E, type1=cupTeamIds@0x24E,
     * type2=tourn.teamIds@0x9A). The 288/2310/148 spans INTENTIONALLY overrun
     * their anchor fields into the following regions (these were flat overlapping
     * views of teamIds/groupMap/slots/teamIndexOrder + stageSnapshot). FLD_BYTES =
     * raw copy from the anchor address — byte-identical to the retired aliases
     * (offsets verified; VERIFY=0 gate). */
    FAC("diy_team_id_list_type_0",        ((DIYCompetitionEx *)&_primaryCompetitionBuf)->matchData.league.leagueTeamIds, 288, FLD_BYTES),
    FAC("diy_team_id_list_type_1",        ((DIYCompetitionEx *)&_primaryCompetitionBuf)->cupTeamIds, 2310, FLD_BYTES),
    FAC("diy_team_id_list_type_2",        _primaryCompetitionBuf.tourn.teamIds, 148, FLD_BYTES),

    /* Per-career-slot competition contexts. Each is a snapshot of
     * diyCompetitionStart / diyFileBufferCopy at the moment
     * Career_StoreCompetitionSlotStateToBuffer was last called for
     * the slot. Sizes are league/hybrid (2900) or cup (1092).
     *
     * These regions contain `namePtr` and other absolute-pointer fields
     * embedded in their DIYCompetition headers. The legacy raw-byte
     * round-trip preserves those pointer values verbatim, which works
     * for same-binary load but breaks across rebuilds (pointer-to-disk
     * problem — see CLAUDE.md). Promoting these to structured DIY
     * fields with sym-encoded inner pointers is future work; for now
     * preserve byte fidelity as flat scalars. */
    /* Primary slot (competitionFileBuffer = primaryCompetitionBuf) — Phase B.
     * Slot 0 is now a standalone CompetitionSlotBuffer, IDENTICAL to the four
     * *Context slots below, so it serializes the same way: ONE full-buffer raw
     * blob here (carries the same-arch tail — tournament teamIds/teamIndexOrder/
     * slots + stageSnapshot that used to ride the careerFileBuffer region dump,
     * now a hole) PLUS the typed emit_comp_slot("primary") cross-target override
     * (header + per-type team-ids + matchData, endian-neutral) and the
     * comp_slot_stage stage serializer (both already emitted in write_career).
     * The old granular primary_comp_* runs (a hand-split header for cross-target
     * safety) are superseded by emit_comp_slot, which corrects every cross-
     * target-sensitive field on load AFTER this blob. namePtr rides the blob
     * (stale absolute pointer) and is re-derived by RebuildPointersAfterLoad. */
    FAC("primary_competition_context",    _primaryCompetitionBuf,       sizeof(CompetitionSlotBuffer), FLD_BYTES),
    /* Context-slot raw blobs — UNTANGLE step A4. The 4 *Context slots are now
     * standalone typed CompetitionSlotBuffer { DIYTournamentState tourn;
     * uint8_t stageSnapshot[DIY_STAGE_SNAPSHOT_LEN]; } (swos_competition_slots.c),
     * no longer packed/aliased in the careerFileBuffer overlay. The blob must
     * therefore cover the whole buffer — sizeof(CompetitionSlotBuffer) tracks
     * the struct (grew when the leagueStandings untangle enlarged stageSnapshot
     * from 0x734 to sizeof(DIYCompetitionEx); ~0xEAE now). The _reserved_5A pad
     * fixes DIYTournamentState at 0x45E on both arches. The old per-overlay sizes
     * (2900/1092) truncated the new layout: tertiary/diy at 1092 didn't even
     * reach the relocated teamIndexOrder@0x41E or the stageSnapshot@0x45E, so
     * those slots lost their seed order + standings across save/load. The
     * comp_slots typed serializer still overrides the cross-target-sensitive
     * header on load; this blob carries the (same-arch) tail. */
    FA("competition_context",          _competitionContextBuf,       sizeof(CompetitionSlotBuffer), FLD_BYTES),
    FA("tertiary_competition_context", _tertiaryCompetitionContextBuf, sizeof(CompetitionSlotBuffer), FLD_BYTES),
    FA("diy_competition_context",      _diyCompetitionContextBuf,    sizeof(CompetitionSlotBuffer), FLD_BYTES),
    FA("euro_cups_competition_context",_euroCupsCompetitionContextBuf, sizeof(CompetitionSlotBuffer), FLD_BYTES),

    /* Team selection key cache — a list of teamSelectionKeyCacheCount
     * variable-length entries. Each entry header: out[0]=teamSlot,
     * out[1]=sortKey, out[2..3]=stride/slotLen (uint16, HOST-NATIVE),
     * out[4..6]=flags, then numGames 12-byte game records whose team-id
     * fields (+0x0C/+0x0E) are BE byte-pairs (BE_WRITE/READ_U16, endian-
     * stable). The ONLY host-native multi-byte field anywhere in the cache
     * is the +2 stride (verified by auditing every *(uint16_t*) cache
     * access). So the byte blob round-trips cross-target EXCEPT the strides
     * byte-swap → the walker's `stride<10||stride>256` bounds check rejects
     * the swapped value → it bails after one entry (observed: only one
     * fixture on the Amiga from a 64-bit cup save). The blob still carries
     * everything correctly; cache_strides (below) re-lays the strides
     * host-native on load. See fixup_cache_strides_on_load. */
    FA("team_selection_key_cache",    _teamSelectionKeyCache,      1870, FLD_BYTES),

    /* Six user-saved tactic slots (teamTacticsUSERA..F). Same 370-byte
     * TeamTactics shape as a .TAC file (see write_tactics_buf). Saved
     * as flat bytes here for table compactness; structured promotion is
     * a candidate follow-up using the existing emit/load helpers.
     *
     * These are in the COMPETITION save set too (comp=2). A team's chosen
     * tactic is stored as an index (TeamFile.tactics; USER A == 12), so a
     * .DIY/.PRE that carried the index but not the six slots reloaded with
     * every team still pointing at USER A while USER A held whatever was in
     * memory — reported as "saving a DIY competition does not save your
     * custom tactic; reloading leaves the selection on USER A but the tactic
     * file isn't loaded into it". The original had them by construction: they
     * live inside the dumped [competitionFileBuffer, numSelectedTeams) region,
     * and LoadDIYFile only re-runs InitUserTactics when the load FAILS
     * (SWOS2.s:125758). */
    FAS("team_tactics_user_a",        _teamTacticsUSERA,            370, FLD_BYTES),
    FAS("team_tactics_user_b",        _teamTacticsUSERB,            370, FLD_BYTES),
    FAS("team_tactics_user_c",        _teamTacticsUSERC,            370, FLD_BYTES),
    FAS("team_tactics_user_d",        _teamTacticsUSERD,            370, FLD_BYTES),
    FAS("team_tactics_user_e",        _teamTacticsUSERE,            370, FLD_BYTES),
    FAS("team_tactics_user_f",        _teamTacticsUSERF,            370, FLD_BYTES),

    /* NOTE: the *Copy competition record region (euroCupCopy..oceania
     * WorldCupQualificationCopy, 34 records) is NO LONGER saved as a raw
     * blob here. It embedded text[2] pointers (width-dependent) → not
     * cross-target safe. It's now reconstructed from the static templates
     * on load (reconstruct_comp_copy_region_from_templates) with the only
     * 2 dynamic bytes carried by emit/load_comp_copy_dynamic. See those. */

    /* Small unstructured tables — internal layout not yet modelled. */
    FA("career_unavailable_player_lookup_table",
                                       _careerUnavailablePlayerLookupTable, 80,  FLD_BYTES),
    FA("career_unavailable_player_names",
                                       _careerUnavailablePlayerNames,     340, FLD_BYTES),

    { NULL, NULL, 0, FLD_END },
};
#undef FX
#undef FA
#undef FXC
#undef FAC
#undef FXS
#undef FAS

static void emit_scalar_table(mjson_print_fn_t fn, void *fnd,
                              const char *section_key,
                              const FieldDesc *table)
{
    mjson_printf(fn, fnd, "%Q:{", section_key);
    int first = 1;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        if (!first) mjson_printf(fn, fnd, "%s", ",");
        first = 0;
        const uint8_t *p = (const uint8_t *)f->addr;
        switch (f->type) {
        case FLD_U8:
            mjson_printf(fn, fnd, "%Q:%u", f->name, (unsigned)*p);
            break;
        case FLD_S8:
            mjson_printf(fn, fnd, "%Q:%d", f->name, (int)*(int8_t *)p);
            break;
        case FLD_U16:
            mjson_printf(fn, fnd, "%Q:%u", f->name, (unsigned)*(uint16_t *)p);
            break;
        case FLD_S16:
            mjson_printf(fn, fnd, "%Q:%d", f->name, (int)*(int16_t *)p);
            break;
        case FLD_U32:
            mjson_printf(fn, fnd, "%Q:%lu", f->name, (long)(uint32_t)*(uint32_t *)p);
            break;
        case FLD_S32:
            mjson_printf(fn, fnd, "%Q:%ld", f->name, (long)*(int32_t *)p);
            break;
        case FLD_STR: {
            uint32_t n = 0;
            while (n < f->size && p[n] != '\0') n++;
            /* Zero post-NUL bytes so the save reflects what the
             * loader will round-trip back. Without this, garbage
             * left over from prior buffer use shows up as
             * verify-time mismatches even though it has no game
             * semantics. */
            for (uint32_t i = n; i < f->size; i++)
                ((uint8_t *)p)[i] = 0;
            mjson_printf(fn, fnd, "%Q:%.*Q", f->name, (int)n, (const char *)p);
            break;
        }
        case FLD_BYTES: {
            const uint32_t len = emitted_len(f, p);
            mjson_printf(fn, fnd, "%Q:[", f->name);
            for (uint32_t i = 0; i < len; i++) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%u", (unsigned)p[i]);
            }
            mjson_printf(fn, fnd, "%s", "]");
            break;
        }
        case FLD_U16_ARRAY:
            /* Emit each 16-bit word as a host-native-read decimal so the
             * on-disk value is byte-order-independent. */
            mjson_printf(fn, fnd, "%Q:[", f->name);
            for (uint32_t i = 0; i + 1 < f->size; i += 2) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%u", (unsigned)*(uint16_t *)(p + i));
            }
            mjson_printf(fn, fnd, "%s", "]");
            break;
        case FLD_U32_ARRAY:
            /* Each 32-bit word as host-native-read decimal — endian-neutral
             * on disk. See FLD_U32_ARRAY in the enum. */
            mjson_printf(fn, fnd, "%Q:[", f->name);
            for (uint32_t i = 0; i + 3 < f->size; i += 4) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%ld", (long)*(int32_t *)(p + i));
            }
            mjson_printf(fn, fnd, "%s", "]");
            break;
        default:
            break;
        }
    }
    mjson_printf(fn, fnd, "%s", "}");
}

/* ---------- Walk-once dispatch for scalar-table loads ----------
 *
 * The legacy loop did `mjson_get_long(section, len, "$.fieldname", ...)`
 * once per FieldDesc entry. mjson_get_long is O(section_len) — it walks
 * the section from the start until it finds the key — so for a 242-entry
 * table over a ~20 KB section that's ~5 MB of byte scanning. On modern
 * x86 nothing; on the Amiga it dominated load time.
 *
 * The new path walks the section once with mjson_next, hashes each key,
 * and looks the FieldDesc up by hash. ~O(section_len) total instead of
 * O(section_len × n_fields). Measured 10–20× speedup at the table-load
 * level on x86; the Amiga win should be larger because the constant
 * factor on mjson_get_long is much worse there. */

typedef struct {
    uint32_t          hash;
    const FieldDesc  *fd;
} HashedField;

static uint32_t fnv1a_bytes(const char *s, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

/* Insertion-sort by hash, in place. n ≤ a few hundred — qsort's
 * function-pointer-per-compare overhead dominates for sizes this small,
 * and we do this once per table (cached across loads). */
static int build_hashed_index(const FieldDesc *table,
                              HashedField *out, int out_cap)
{
    int n = 0;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        if (n >= out_cap) return -1;
        out[n].hash = fnv1a_bytes(f->name, (int)strlen(f->name));
        out[n].fd   = f;
        n++;
    }
    for (int i = 1; i < n; i++) {
        HashedField cur = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].hash > cur.hash) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = cur;
    }
    return n;
}

static const FieldDesc *find_field_by_hash(uint32_t h,
                                           const HashedField *arr, int n)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t mh = arr[mid].hash;
        if (mh < h) lo = mid + 1;
        else if (mh > h) hi = mid - 1;
        else return arr[mid].fd;
    }
    return NULL;
}

/* Parse a decimal integer from a slice (number-token value bytes from
 * mjson_next). Faster than calling mjson_get_long on the slice — the
 * state-machine pass has per-call overhead we don't need. */
static long parse_long_slice(const char *s, int n)
{
    long v = 0;
    int sign = 1, i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i < n && s[i] == '-') { sign = -1; i++; }
    else if (i < n && s[i] == '+')        i++;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') break;
        v = v * 10 + (s[i] - '0');
    }
    return sign * v;
}

/* Sequential JSON object key/value iterator.
 *
 * mjson_next() walks the slice from char 0 on every call to find the
 * next token after `pos`, so walking N keys is O(N²) total. For tight
 * loops over fixed-schema objects (TeamFile, PlayerFile, MarketPlayer)
 * called thousands of times per career load, that quadratic cost
 * dominated the load profile after the FLD_BYTES fast walker landed.
 *
 * fast_object_next maintains the previously-returned position itself
 * — single linear pass through the slice characters, no callbacks, no
 * depth tracking outside what's needed for nested object/array
 * skipping. ~5-10× faster than mjson_next on small fixed-schema
 * objects.
 *
 * Input/output semantics match mjson_next:
 *   - Returns new pos after the value, or 0 at end of object.
 *   - Sets koff/klen to the key bytes INCLUDING surrounding quotes
 *     (caller strips them) — matches mjson_next convention.
 *   - Sets voff/vlen to the value's byte range (including surrounding
 *     brackets/quotes for object/array/string values).
 *   - Sets vt to one of MJSON_TOK_{NUMBER,STRING,OBJECT,ARRAY,TRUE,
 *     FALSE,NULL}. */
static int fast_object_next(const char *slice, int slen, int pos,
                            int *koff, int *klen,
                            int *voff, int *vlen, int *vt)
{
    /* Skip whitespace, comma separators, and a leading '{' on the
     * very first call (pos == 0). */
    while (pos < slen) {
        char c = slice[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == ',' || c == '{') { pos++; continue; }
        break;
    }
    if (pos >= slen || slice[pos] == '}') return 0;
    if (slice[pos] != '"') return 0;

    /* Key: koff/klen include the surrounding quotes to match
     * mjson_next's convention. */
    int kstart = pos;
    pos++;  /* past opening quote */
    while (pos < slen && slice[pos] != '"') {
        if (slice[pos] == '\\' && pos + 1 < slen) pos++;
        pos++;
    }
    if (pos >= slen) return 0;
    pos++;  /* past closing quote */
    *koff = kstart;
    *klen = pos - kstart;

    /* Skip whitespace + ':' between key and value. */
    while (pos < slen) {
        char c = slice[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ':') {
            pos++;
            continue;
        }
        break;
    }
    if (pos >= slen) return 0;

    int vstart = pos;
    char vc = slice[pos];
    if (vc == '"') {
        *vt = MJSON_TOK_STRING;
        pos++;
        while (pos < slen && slice[pos] != '"') {
            if (slice[pos] == '\\' && pos + 1 < slen) pos++;
            pos++;
        }
        if (pos < slen) pos++;  /* past closing quote */
    } else if (vc == '{' || vc == '[') {
        *vt = (vc == '{') ? MJSON_TOK_OBJECT : MJSON_TOK_ARRAY;
        int depth = 1;
        pos++;
        while (pos < slen && depth > 0) {
            char c = slice[pos];
            if (c == '"') {
                /* Skip a nested string so its braces/brackets don't
                 * confuse depth tracking. */
                pos++;
                while (pos < slen && slice[pos] != '"') {
                    if (slice[pos] == '\\' && pos + 1 < slen) pos++;
                    pos++;
                }
            } else if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                depth--;
            }
            pos++;
        }
    } else if ((vc >= '0' && vc <= '9') || vc == '-' || vc == '+') {
        *vt = MJSON_TOK_NUMBER;
        pos++;
        while (pos < slen) {
            char c = slice[pos];
            if ((c >= '0' && c <= '9') || c == '.' ||
                c == 'e' || c == 'E' || c == '+' || c == '-') {
                pos++;
                continue;
            }
            break;
        }
    } else if (vc == 't' || vc == 'f' || vc == 'n') {
        *vt = (vc == 't') ? MJSON_TOK_TRUE
            : (vc == 'f') ? MJSON_TOK_FALSE
            :               MJSON_TOK_NULL;
        while (pos < slen) {
            char c = slice[pos];
            if (c >= 'a' && c <= 'z') { pos++; continue; }
            break;
        }
    } else {
        /* Unknown — bail to prevent an infinite loop. */
        return 0;
    }
    *voff = vstart;
    *vlen = pos - vstart;
    return pos;
}

/* Sequential JSON array element iterator — companion to
 * fast_object_next. Same O(N) total cost vs mjson_next's O(N²) when
 * walking N elements of an array. Used for the big arrays in
 * apply_semantic_load (selected_teams, tmd_teams,
 * career_foreign_market_players, reserves) which each hold hundreds
 * of element objects. */
static int fast_array_next(const char *slice, int slen, int pos,
                           int *voff, int *vlen, int *vt)
{
    /* Skip whitespace and commas, plus the array's OWN opening '[' -- but that
     * one ONLY at the very start of the slice.
     *
     * This used to skip every leading '[', which is right for a flat array and
     * wrong for an array OF arrays: it also ate each element's own opening
     * bracket, so the scanner landed on the first number inside and reported
     * MJSON_TOK_NUMBER instead of MJSON_TOK_ARRAY. Callers that test for
     * MJSON_TOK_ARRAY therefore rejected every element and silently restored
     * nothing -- that is what made comp_copy_teamlists a no-op on load, leaving
     * every cup on its static template (see load_comp_copy_region). */
    int at_start = (pos == 0);
    while (pos < slen) {
        char c = slice[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
            pos++; continue;
        }
        if (c == '[' && at_start) { pos++; at_start = 0; continue; }
        break;
    }
    if (pos >= slen || slice[pos] == ']') return 0;

    int vstart = pos;
    char vc = slice[pos];
    if (vc == '"') {
        *vt = MJSON_TOK_STRING;
        pos++;
        while (pos < slen && slice[pos] != '"') {
            if (slice[pos] == '\\' && pos + 1 < slen) pos++;
            pos++;
        }
        if (pos < slen) pos++;
    } else if (vc == '{' || vc == '[') {
        *vt = (vc == '{') ? MJSON_TOK_OBJECT : MJSON_TOK_ARRAY;
        int depth = 1;
        pos++;
        while (pos < slen && depth > 0) {
            char c = slice[pos];
            if (c == '"') {
                pos++;
                while (pos < slen && slice[pos] != '"') {
                    if (slice[pos] == '\\' && pos + 1 < slen) pos++;
                    pos++;
                }
            } else if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                depth--;
            }
            pos++;
        }
    } else if ((vc >= '0' && vc <= '9') || vc == '-' || vc == '+') {
        *vt = MJSON_TOK_NUMBER;
        pos++;
        while (pos < slen) {
            char c = slice[pos];
            if ((c >= '0' && c <= '9') || c == '.' ||
                c == 'e' || c == 'E' || c == '+' || c == '-') {
                pos++;
                continue;
            }
            break;
        }
    } else if (vc == 't' || vc == 'f' || vc == 'n') {
        *vt = (vc == 't') ? MJSON_TOK_TRUE
            : (vc == 'f') ? MJSON_TOK_FALSE
            :               MJSON_TOK_NULL;
        while (pos < slen) {
            char c = slice[pos];
            if (c >= 'a' && c <= 'z') { pos++; continue; }
            break;
        }
    } else {
        return 0;
    }
    *voff = vstart;
    *vlen = pos - vstart;
    return pos;
}

/* Parse a `[N,N,N,...]` JSON array of unsigned bytes (0-255) directly
 * into a destination buffer. Replaces the mjson_next-per-element loop
 * which is O(N²) per array because each mjson_next call walks the
 * whole array from the start to find the next element. This walker is
 * O(N) total — single linear pass over the slice characters, picking
 * out integers between commas. On the career_scalars FLD_BYTES regions
 * (10+ KB of byte arrays totalling ~20K elements) this drops the inner
 * load cost from ~200 ms to sub-millisecond on x86. */
static void parse_byte_array_slice(const char *slice, int slen,
                                   uint8_t *dst, uint32_t max_count)
{
    const char *p   = slice;
    const char *end = slice + slen;
    uint32_t    i   = 0;

    /* Skip the opening '['. */
    while (p < end && *p != '[') p++;
    if (p < end) p++;

    while (p < end && i < max_count) {
        /* Skip whitespace + comma separators. */
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == ',' || c == '\t' ||
                c == '\n' || c == '\r') { p++; continue; }
            break;
        }
        if (p >= end || *p == ']') break;

        /* Parse one unsigned decimal integer. Values are emitted via
         * `%u` from emit_scalar_table's FLD_BYTES branch so they're
         * always 0-255 with no sign and no fractional part. */
        long v = 0;
        if (*p == '-') {
            /* Defensive — values are unsigned but the parser shouldn't
             * trip on stray minus signs. */
            p++;
            while (p < end && *p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
            }
            v = -v;
        } else {
            while (p < end && *p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
            }
        }
        dst[i++] = (uint8_t)v;
    }
}

/* Parse a `[N,N,...]` JSON array of decimal 16-bit words into `dst`,
 * writing each as a host-native uint16. Companion to FLD_U16_ARRAY's
 * emit: the on-disk values are byte-order-independent decimals, so the
 * load stores them in this host's byte order regardless of the saving
 * host's endianness. `max_bytes` is the destination size in BYTES
 * (max_bytes/2 words written). */
static void parse_u16_array_slice(const char *slice, int slen,
                                  uint8_t *dst, uint32_t max_bytes)
{
    const char *p   = slice;
    const char *end = slice + slen;
    uint32_t    off = 0;

    while (p < end && *p != '[') p++;
    if (p < end) p++;

    while (p < end && off + 1 < max_bytes) {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == ',' || c == '\t' ||
                c == '\n' || c == '\r') { p++; continue; }
            break;
        }
        if (p >= end || *p == ']') break;

        long v = 0;
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        if (neg) v = -v;
        *(uint16_t *)(dst + off) = (uint16_t)v;
        off += 2;
    }
}

/* Parse a `[N,N,...]` JSON array of decimal 32-bit words into `dst`,
 * host-native int32. Companion to FLD_U32_ARRAY's emit (endian-neutral
 * decimals). `max_bytes` is the destination size in BYTES (max_bytes/4
 * words written). Mirrors parse_u16_array_slice but 32-bit + signed. */
static void parse_u32_array_slice(const char *slice, int slen,
                                  uint8_t *dst, uint32_t max_bytes)
{
    const char *p   = slice;
    const char *end = slice + slen;
    uint32_t    off = 0;

    while (p < end && *p != '[') p++;
    if (p < end) p++;

    while (p < end && off + 3 < max_bytes) {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == ',' || c == '\t' ||
                c == '\n' || c == '\r') { p++; continue; }
            break;
        }
        if (p >= end || *p == ']') break;

        long v = 0;
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        if (neg) v = -v;
        *(int32_t *)(dst + off) = (int32_t)v;
        off += 4;
    }
}

static void load_scalar_table(const char *buf, int buflen,
                              const char *section_key,
                              const FieldDesc *table)
{
    /* Resolve the section's object slice ONCE. */
    char section_path[80];
    swos_io_snprintf(section_path, sizeof(section_path), "$.%s", section_key);
    const char *section = NULL;
    int section_len = 0;
    if (mjson_find(buf, buflen, section_path,
                   &section, &section_len) != MJSON_TOK_OBJECT)
        return;

    /* Hashed-index cache — currently we have one FieldDesc table
     * (s_career_scalars, 242 entries). A single-slot cache keyed by
     * the table pointer is enough; if/when more tables are added the
     * cache can grow to a small array. */
    static HashedField        s_hashed_index[300];
    static const FieldDesc   *s_indexed_table = NULL;
    static int                s_indexed_count = 0;
    if (s_indexed_table != table) {
        s_indexed_count = build_hashed_index(table, s_hashed_index,
                                             (int)(sizeof(s_hashed_index) /
                                                   sizeof(s_hashed_index[0])));
        if (s_indexed_count < 0) return;  /* table outgrew cache */
        s_indexed_table = table;
    }

#if MJSON_ENABLE_NEXT && !defined(SWOS_SAVE_LEGACY_DISPATCH)
    /* Walk every key/value pair in the section once, dispatch via hash
     * lookup into the FieldDesc index. Unknown keys are silently
     * skipped; absent FieldDesc entries leave their target unchanged
     * (which matches the legacy loop's `if (mjson_get_*) ...` early-out
     * semantics). */
    char tmp[64];
    int pos = 0, koff, klen, voff, vlen, vt;
    while ((pos = mjson_next(section, section_len, pos,
                             &koff, &klen, &voff, &vlen, &vt)) != 0) {
        /* koff..koff+klen wraps the key in double quotes — strip them
         * for the hash. */
        if (klen < 2) continue;
        const char *key  = section + koff + 1;
        int         keyl = klen - 2;
        uint32_t    h    = fnv1a_bytes(key, keyl);
        const FieldDesc *f = find_field_by_hash(h, s_hashed_index, s_indexed_count);
        if (!f) continue;
        /* Hash collision guard: verify the name actually matches. */
        if ((int)strlen(f->name) != keyl ||
            memcmp(f->name, key, (size_t)keyl) != 0) continue;

        uint8_t   *p  = (uint8_t *)f->addr;
        const char *vs = section + voff;
        int         vl = vlen;

        switch (f->type) {
        case FLD_U8: case FLD_S8:
            if (vt == MJSON_TOK_NUMBER)
                *p = (uint8_t)parse_long_slice(vs, vl);
            break;
        case FLD_U16: case FLD_S16:
            if (vt == MJSON_TOK_NUMBER)
                *(uint16_t *)p = (uint16_t)parse_long_slice(vs, vl);
            break;
        case FLD_U32: case FLD_S32:
            if (vt == MJSON_TOK_NUMBER)
                *(uint32_t *)p = (uint32_t)parse_long_slice(vs, vl);
            break;
        case FLD_STR:
            memset(p, 0, f->size);
            if (vt == MJSON_TOK_STRING) {
                /* Use mjson_get_string on the value slice to handle
                 * any backslash escapes correctly. The slice is tiny
                 * (≤64 bytes typically) so this is cheap. */
                if (mjson_get_string(vs, vl, "$", tmp, (int)sizeof(tmp)) > 0) {
                    uint32_t n = (uint32_t)strlen(tmp);
                    if (n > f->size) n = f->size;
                    memcpy(p, tmp, n);
                }
            }
            break;
        case FLD_BYTES:
            if (vt == MJSON_TOK_ARRAY)
                parse_byte_array_slice(vs, vl, p, f->size);
            break;
        case FLD_U16_ARRAY:
            if (vt == MJSON_TOK_ARRAY)
                parse_u16_array_slice(vs, vl, p, f->size);
            break;
        case FLD_U32_ARRAY:
            if (vt == MJSON_TOK_ARRAY)
                parse_u32_array_slice(vs, vl, p, f->size);
            break;
        default:
            break;
        }
    }
#else
    /* Legacy per-field path-lookup loop. Each mjson_get_long is
     * O(section_len) — for the 242-entry s_career_scalars over a
     * ~20 KB section that's ~5 MB of byte scanning per load. The new
     * walk-once dispatch above is ~10-20× faster. This path is kept
     * as a comparison baseline (build with SWOS_SAVE_LEGACY_DISPATCH=1)
     * and as a fallback for builds without MJSON_ENABLE_NEXT. */
    char path[80];
    char tmp[64];
    long num = 0;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        swos_io_snprintf(path, sizeof(path), "$.%s", f->name);
        uint8_t *p = (uint8_t *)f->addr;
        switch (f->type) {
        case FLD_U8: case FLD_S8:
            if (mjson_get_long(section, section_len, path, &num) > 0)
                *p = (uint8_t)(int)num;
            break;
        case FLD_U16: case FLD_S16:
            if (mjson_get_long(section, section_len, path, &num) > 0)
                *(uint16_t *)p = (uint16_t)(int)num;
            break;
        case FLD_U32: case FLD_S32:
            if (mjson_get_long(section, section_len, path, &num) > 0)
                *(uint32_t *)p = (uint32_t)(int32_t)num;
            break;
        case FLD_STR:
            memset(p, 0, f->size);
            if (mjson_get_string(section, section_len, path, tmp, sizeof(tmp)) > 0) {
                uint32_t n = (uint32_t)strlen(tmp);
                if (n > f->size) n = f->size;
                memcpy(p, tmp, n);
            }
            break;
        case FLD_BYTES: {
#if MJSON_ENABLE_NEXT
            const char *arr = NULL;
            int arr_len = 0;
            if (mjson_find(section, section_len, path,
                           &arr, &arr_len) == MJSON_TOK_ARRAY) {
                int koff, klen, voff, vlen, vt;
                uint32_t i = 0;
                int off = 0;
                while (i < f->size &&
                       (off = mjson_next(arr, arr_len, off,
                                         &koff, &klen, &voff, &vlen, &vt)) != 0) {
                    if (vt == MJSON_TOK_NUMBER) {
                        char nbuf[32];
                        int n = vlen < (int)sizeof(nbuf) - 1 ? vlen : (int)sizeof(nbuf) - 1;
                        memcpy(nbuf, arr + voff, (size_t)n);
                        nbuf[n] = '\0';
                        p[i] = (uint8_t)atoi(nbuf);
                    }
                    i++;
                }
            }
#endif
            break;
        }
        default:
            break;
        }
    }
#endif
}

/* ---------- TeamFileEx semantic encoder/decoder ----------
 *
 * TeamFileEx is a 684-byte struct: 76-byte TeamFile header + 16
 * 38-byte PlayerFile entries. Used for careerTeam, currentTeam, and
 * the selTeamsPtr array (16 TeamFileEx records following
 * numSelectedTeams). Encoded as:
 *
 *   { "team": { ... named fields; kit_colors split into 10 primary_/secondary_ slots ... },
 *     "players": [ 16 × { ... named fields; packed bytes split for editing:
 *                  position/face/flags, cards/injury/status_bucket, and the three skill
 *                  nibble-pairs velocity/heading, tackling/control, speed/finishing ... } ] }
 *
 * Offsets via offsetof() — survives the 64-bit migration because
 * both struct layouts are packed (no padding to shift).
 */

extern uint8_t _careerTeam;       /* TeamFileEx at careerFileBuffer + 56190 */
extern uint8_t _currentTeam;      /* TeamFileEx at careerFileBuffer + 61610 */

/* Large opaque region externs — emitted as named base64 sub-blobs
 * so the JSON has navigable structure for each region instead of
 * one big opaque_data field. Bodies stay byte-identical to legacy;
 * future promotions replace each named blob with a typed encoder. */
extern uint8_t _tmdFileBuffer[];                     /* +2,     54720 B */
extern uint8_t _careerUnavailablePlayerLookupTable[];/* +55738, 80 B  */
extern uint8_t _careerUnavailablePlayerNames[];      /* +55818, 340 B */
extern uint8_t _diyLeagueSeasonStateTable[];         /* +59610, 2000 B */
extern CompetitionSlotBuffer _competitionContextBuf;         /* slot-1 untangle (S4): typed buffer (was +63340, 2900 B) */
extern CompetitionSlotBuffer _tertiaryCompetitionContextBuf;  /* slot-2 untangle (S3): typed buffer (was +66240, 1092 B) */
extern CompetitionSlotBuffer _diyCompetitionContextBuf;  /* slot-4 untangle (S2): typed buffer (was +67332, 1092 B) */
extern CompetitionSlotBuffer _euroCupsCompetitionContextBuf; /* slot-3 untangle (S5): typed buffer (was +68424, 2900 B) */
extern uint8_t _teamSelectionKeyCache[];             /* +73292, ~1870 B */
extern uint8_t _careerForeignMarketPlayers[];        /* +75162, 18144 B */
/* diyTeamIdListType2/0/1 reached via primaryCompetitionBuf casts now (S6). */
extern uint8_t _teamTacticsUSERA[];                  /* +96220, 370 B */
extern uint8_t _teamTacticsUSERB[];                  /* +96590, 370 B */
extern uint8_t _teamTacticsUSERC[];                  /* +96960, 370 B */
extern uint8_t _teamTacticsUSERD[];                  /* +97330, 370 B */
extern uint8_t _teamTacticsUSERE[];                  /* +97700, 370 B */
extern uint8_t _teamTacticsUSERF[];                  /* +98070, 370 B */

/* *Copy slot region — one big contiguous block at +71452, 1832 B.
 * Individual records resolvable via careerContests; we emit the
 * whole region as one blob since the nameSource pointers (the only
 * pointer-typed fields) are already promoted separately. */
extern uint8_t _euroCupCopy[];                       /* +71452 */

/* Helpers for writing/reading fields at struct offsets. */
static void emit_str_at(mjson_print_fn_t fn, void *fnd,
                        const char *key, const uint8_t *base,
                        size_t off, size_t maxlen)
{
    uint8_t *p = (uint8_t *)base + off;
    size_t n = 0;
    while (n < maxlen && p[n] != '\0') n++;
    /* Zero post-NUL bytes — see comment in emit_scalar_table.
     * Same rationale: lose nothing semantic, gain byte-exact
     * verify round-trip. */
    for (size_t i = n; i < maxlen; i++) p[i] = 0;
    mjson_printf(fn, fnd, "%Q:%.*Q", key, (int)n, (const char *)p);
}

static void emit_u8_at(mjson_print_fn_t fn, void *fnd,
                       const char *key, const uint8_t *base, size_t off)
{
    mjson_printf(fn, fnd, "%Q:%u", key, (unsigned)base[off]);
}

static void emit_s8_at(mjson_print_fn_t fn, void *fnd,
                       const char *key, const uint8_t *base, size_t off)
{
    mjson_printf(fn, fnd, "%Q:%d", key, (int)(int8_t)base[off]);
}

/* Emit one nibble of a packed byte as an editable JSON number (0-15). */
static void emit_nibble_hi_at(mjson_print_fn_t fn, void *fnd,
                              const char *key, const uint8_t *base, size_t off)
{
    mjson_printf(fn, fnd, "%Q:%u", key, (unsigned)((base[off] >> 4) & 0x0F));
}

static void emit_nibble_lo_at(mjson_print_fn_t fn, void *fnd,
                              const char *key, const uint8_t *base, size_t off)
{
    mjson_printf(fn, fnd, "%Q:%u", key, (unsigned)(base[off] & 0x0F));
}

/* Right-justify shift for a contiguous bit mask (trailing-zero count). */
static int mask_shift(unsigned mask)
{
    int s = 0;
    if (!mask) return 0;
    while (!(mask & 1u)) { mask >>= 1; s++; }
    return s;
}

/* Emit one contiguous bit-run (selected by `mask`) of a packed byte, right-justified. */
static void emit_bits_at(mjson_print_fn_t fn, void *fnd, const char *key,
                         const uint8_t *base, size_t off, unsigned mask)
{
    mjson_printf(fn, fnd, "%Q:%u", key,
                 (unsigned)((base[off] & mask) >> mask_shift(mask)));
}

static void emit_s16_at(mjson_print_fn_t fn, void *fnd,
                        const char *key, const uint8_t *base, size_t off)
{
    mjson_printf(fn, fnd, "%Q:%d", key, (int)*(int16_t *)(base + off));
}

static void emit_u16_at(mjson_print_fn_t fn, void *fnd,
                        const char *key, const uint8_t *base, size_t off)
{
    uint16_t v; memcpy(&v, base + off, sizeof(v));   /* memcpy: packed-array elements may be unaligned */
    mjson_printf(fn, fnd, "%Q:%u", key, (unsigned)v);
}

static void emit_u32_at(mjson_print_fn_t fn, void *fnd,
                        const char *key, const uint8_t *base, size_t off)
{
    uint32_t v; memcpy(&v, base + off, sizeof(v));
    mjson_printf(fn, fnd, "%Q:%lu", key, (unsigned long)v);
}

static void emit_bytes_at(mjson_print_fn_t fn, void *fnd,
                          const char *key, const uint8_t *base,
                          size_t off, size_t len)
{
    mjson_printf(fn, fnd, "%Q:[", key);
    for (size_t i = 0; i < len; i++) {
        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        mjson_printf(fn, fnd, "%u", (unsigned)base[off + i]);
    }
    mjson_printf(fn, fnd, "%s", "]");
}

static void load_str_at(const char *buf, int buflen, const char *path,
                        uint8_t *base, size_t off, size_t maxlen)
{
    uint8_t *p = base + off;
    memset(p, 0, maxlen);
    char tmp[64];
    if (mjson_get_string(buf, buflen, path, tmp, sizeof(tmp)) > 0) {
        size_t n = strlen(tmp);
        /* Fields can fill all `maxlen` bytes with no NUL terminator
         * (e.g. a 22-char playerName that maxes out the slot). Copy
         * up to maxlen bytes — don't reserve a NUL slot. */
        if (n > maxlen) n = maxlen;
        memcpy(p, tmp, n);
    }
}

static void load_u8_at(const char *buf, int buflen, const char *path,
                       uint8_t *base, size_t off)
{
    long num = 0.0;
    if (mjson_get_long(buf, buflen, path, &num) > 0)
        base[off] = (uint8_t)(int)num;
}

static void load_s16_at(const char *buf, int buflen, const char *path,
                        uint8_t *base, size_t off)
{
    long num = 0.0;
    if (mjson_get_long(buf, buflen, path, &num) > 0)
        *(int16_t *)(base + off) = (int16_t)num;
}

static void load_bytes_at(const char *buf, int buflen, const char *path,
                          uint8_t *base, size_t off, size_t len)
{
#if MJSON_ENABLE_NEXT
    const char *arr = NULL;
    int arr_len = 0;
    if (mjson_find(buf, buflen, path, &arr, &arr_len) == MJSON_TOK_ARRAY) {
        int koff, klen, voff, vlen, vt;
        size_t i = 0;
        int o = 0;
        while (i < len &&
               (o = mjson_next(arr, arr_len, o,
                               &koff, &klen, &voff, &vlen, &vt)) != 0) {
            if (vt == MJSON_TOK_NUMBER) {
                char nbuf[32];
                int n = vlen < (int)sizeof(nbuf) - 1 ? vlen : (int)sizeof(nbuf) - 1;
                memcpy(nbuf, arr + voff, (size_t)n);
                nbuf[n] = '\0';
                base[off + i] = (uint8_t)atoi(nbuf);
            }
            i++;
        }
    }
#else
    (void)buf; (void)buflen; (void)path; (void)base; (void)off; (void)len;
#endif
}

/* Emit a single PlayerFile (38 B) as a JSON object body. Used by the
 * TeamFileEx players[] loop and by typed market-player encoders. */
static void emit_player_file_body(mjson_print_fn_t fn, void *fnd,
                                  const uint8_t *p)
{
    mjson_printf(fn, fnd, "%s", "{");
    emit_s8_at  (fn, fnd, "nationality",          p, offsetof(PlayerFile, nationality));
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "cached_match_value",   p, offsetof(PlayerFile, cachedMatchValue));
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "shirt_number",         p, offsetof(PlayerFile, shirtNumber));
    mjson_printf(fn, fnd, "%s", ",");
    /* player_name now 23 bytes: the 22-char name + its NUL terminator (the former
     * separate "field_19" byte, folded into the name buffer 2026-06-19). */
    emit_str_at (fn, fnd, "player_name",          p, offsetof(PlayerFile, playerName), 23);
    mjson_printf(fn, fnd, "%s", ",");
    /* positionAndFace byte split: position (bits 5-7), face (bits 3-4), flags (bits 0-2:
     * inform/loan/trialist — runtime, 0 in static team data). 2026-06-22. */
    emit_bits_at(fn, fnd, "position", p, offsetof(PlayerFile, positionAndFace), PF_POSITION_MASK);
    mjson_printf(fn, fnd, "%s", ",");
    emit_bits_at(fn, fnd, "face",     p, offsetof(PlayerFile, positionAndFace), PF_FACE_MASK);
    mjson_printf(fn, fnd, "%s", ",");
    emit_bits_at(fn, fnd, "flags",    p, offsetof(PlayerFile, positionAndFace), 0x07);
    mjson_printf(fn, fnd, "%s", ",");
    /* cardsInjuries byte split: cards (bits 0-1), injury (bits 2-4), status_bucket
     * (bits 5-7) — all runtime career state, 0 in static team data. */
    emit_bits_at(fn, fnd, "cards",         p, offsetof(PlayerFile, cardsInjuries), 0x03);
    mjson_printf(fn, fnd, "%s", ",");
    emit_bits_at(fn, fnd, "injury",        p, offsetof(PlayerFile, cardsInjuries), 0x1C);
    mjson_printf(fn, fnd, "%s", ",");
    emit_bits_at(fn, fnd, "status_bucket", p, offsetof(PlayerFile, cardsInjuries), 0xE0);
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "passing",              p, offsetof(PlayerFile, passing));
    mjson_printf(fn, fnd, "%s", ",");
    /* The three skill bytes are nibble pairs (hi=first label, lo=second — see the
     * PlayerFile struct comments + match_sim.c skill-unpack). Split into six editable
     * fields 2026-06-22; load recombines them. (Pre-split saves used the packed keys
     * velocity_heading/tackling_control/speed_finishing — still read for back-compat.) */
    emit_nibble_hi_at(fn, fnd, "velocity",  p, offsetof(PlayerFile, velocityHeading));
    mjson_printf(fn, fnd, "%s", ",");
    emit_nibble_lo_at(fn, fnd, "heading",   p, offsetof(PlayerFile, velocityHeading));
    mjson_printf(fn, fnd, "%s", ",");
    emit_nibble_hi_at(fn, fnd, "tackling",  p, offsetof(PlayerFile, tacklingControl));
    mjson_printf(fn, fnd, "%s", ",");
    emit_nibble_lo_at(fn, fnd, "control",   p, offsetof(PlayerFile, tacklingControl));
    mjson_printf(fn, fnd, "%s", ",");
    emit_nibble_hi_at(fn, fnd, "speed",     p, offsetof(PlayerFile, speedFinishing));
    mjson_printf(fn, fnd, "%s", ",");
    emit_nibble_lo_at(fn, fnd, "finishing", p, offsetof(PlayerFile, speedFinishing));
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "price",                p, offsetof(PlayerFile, price));
    mjson_printf(fn, fnd, "%s", ",");
    emit_bytes_at(fn, fnd,"goal_stats",           p, offsetof(PlayerFile, goalStats), 4);
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "form",                 p, offsetof(PlayerFile, form));
    mjson_printf(fn, fnd, "%s", "}");
}

/* ---------- Walk-once dispatch for struct-relative fields ----------
 *
 * Same idea as load_scalar_table's hash-dispatch but for fields whose
 * addresses are computed as `base_ptr + offset` instead of being
 * absolute. Used for the team/player loaders called many times per
 * career load (32 selected teams × 16 players each, plus tmd_teams
 * which can be 80+). Walks each JSON object once via mjson_next and
 * looks up each key in a small per-struct table via linear search —
 * tables are short (~13 entries) so a hash index isn't worth the
 * cache pressure. */
typedef struct {
    const char *name;
    uint16_t    offset;
    uint16_t    size;     /* for STR / BYTES; ignored for scalars */
    FieldType   type;
} StructFieldDesc;

static void load_struct_object(const char *slice, int slen, uint8_t *base,
                               const StructFieldDesc *table)
{
    char tmp[64];
    int pos = 0, koff, klen, voff, vlen, vt;
    while ((pos = fast_object_next(slice, slen, pos,
                                   &koff, &klen, &voff, &vlen, &vt)) != 0) {
        if (klen < 2) continue;
        const char *k = slice + koff + 1;
        int         kl = klen - 2;
        const char *vs = slice + voff;
        int         vl = vlen;

        const StructFieldDesc *f = NULL;
        for (const StructFieldDesc *e = table; e->name; e++) {
            if (k[0] == e->name[0] &&
                kl == (int)strlen(e->name) &&
                memcmp(e->name, k, (size_t)kl) == 0) {
                f = e;
                break;
            }
        }
        if (!f) continue;

        uint8_t *p = base + f->offset;
        switch (f->type) {
        case FLD_U8: case FLD_S8:
            if (vt == MJSON_TOK_NUMBER)
                *p = (uint8_t)parse_long_slice(vs, vl);
            break;
        case FLD_NIBBLE_HI:   /* two halves share one offset; RMW so the byte survives */
            if (vt == MJSON_TOK_NUMBER)
                *p = (uint8_t)((*p & 0x0F) | (((uint8_t)parse_long_slice(vs, vl) & 0x0F) << 4));
            break;
        case FLD_NIBBLE_LO:
            if (vt == MJSON_TOK_NUMBER)
                *p = (uint8_t)((*p & 0xF0) | ((uint8_t)parse_long_slice(vs, vl) & 0x0F));
            break;
        case FLD_BITS:        /* contiguous bit-run; mask = f->size; RMW */
            if (vt == MJSON_TOK_NUMBER) {
                unsigned mask = f->size;
                int      sh   = mask_shift(mask);
                *p = (uint8_t)((*p & ~mask) |
                               (((unsigned)parse_long_slice(vs, vl) << sh) & mask));
            }
            break;
        case FLD_U16: case FLD_S16:
            if (vt == MJSON_TOK_NUMBER) {
                uint16_t v = (uint16_t)parse_long_slice(vs, vl);
                memcpy(p, &v, sizeof(v));
            }
            break;
        case FLD_U32: case FLD_S32:
            if (vt == MJSON_TOK_NUMBER) {
                uint32_t v = (uint32_t)parse_long_slice(vs, vl);
                memcpy(p, &v, sizeof(v));
            }
            break;
        case FLD_STR:
            memset(p, 0, f->size);
            if (vt == MJSON_TOK_STRING) {
                if (mjson_get_string(vs, vl, "$", tmp, (int)sizeof(tmp)) > 0) {
                    uint32_t n = (uint32_t)strlen(tmp);
                    if (n > f->size) n = f->size;
                    memcpy(p, tmp, n);
                }
            }
            break;
        case FLD_BYTES:
            if (vt == MJSON_TOK_ARRAY)
                parse_byte_array_slice(vs, vl, p, f->size);
            break;
        default:
            break;
        }
    }
}

/* Generic table-driven emitter — mirror of load_struct_object. Emits a JSON
 * object { "field":value, ... } for every entry in `table`, using the typed
 * emit helpers so multi-byte fields cross-target safely (decimal numbers are
 * host-order-independent). Every table entry must use a handled type. */
static void emit_struct_object(mjson_print_fn_t fn, void *fnd,
                               const uint8_t *base, const StructFieldDesc *table)
{
    mjson_printf(fn, fnd, "%s", "{");
    int first = 1;
    for (const StructFieldDesc *f = table; f->name; f++) {
        if (!first) mjson_printf(fn, fnd, "%s", ",");
        first = 0;
        switch (f->type) {
        case FLD_U8:    emit_u8_at   (fn, fnd, f->name, base, f->offset); break;
        case FLD_S8:    emit_s8_at   (fn, fnd, f->name, base, f->offset); break;
        case FLD_NIBBLE_HI: emit_nibble_hi_at(fn, fnd, f->name, base, f->offset); break;
        case FLD_NIBBLE_LO: emit_nibble_lo_at(fn, fnd, f->name, base, f->offset); break;
        case FLD_BITS:  emit_bits_at(fn, fnd, f->name, base, f->offset, f->size); break;
        case FLD_U16:   emit_u16_at  (fn, fnd, f->name, base, f->offset); break;
        case FLD_S16:   emit_s16_at  (fn, fnd, f->name, base, f->offset); break;
        case FLD_U32:
        case FLD_S32:   emit_u32_at  (fn, fnd, f->name, base, f->offset); break;
        case FLD_STR:   emit_str_at  (fn, fnd, f->name, base, f->offset, f->size); break;
        case FLD_BYTES: emit_bytes_at(fn, fnd, f->name, base, f->offset, f->size); break;
        default:        mjson_printf(fn, fnd, "%Q:0", f->name); break;
        }
    }
    mjson_printf(fn, fnd, "%s", "}");
}

/* PlayerFile field table — keys must match emit_player_file_body. */
static const StructFieldDesc s_player_fields[] = {
    { "nationality",          offsetof(PlayerFile, nationality),         0, FLD_U8 },
    { "cached_match_value",   offsetof(PlayerFile, cachedMatchValue),    0, FLD_U8 },
    { "shirt_number",         offsetof(PlayerFile, shirtNumber),         0, FLD_U8 },
    { "player_name",          offsetof(PlayerFile, playerName),         23, FLD_STR },
    /* positionAndFace bits: position (5-7), face (3-4), flags (0-2). */
    { "position",             offsetof(PlayerFile, positionAndFace), PF_POSITION_MASK, FLD_BITS },
    { "face",                 offsetof(PlayerFile, positionAndFace), PF_FACE_MASK,     FLD_BITS },
    { "flags",                offsetof(PlayerFile, positionAndFace), 0x07,             FLD_BITS },
    /* cardsInjuries bits: cards (0-1), injury (2-4), status_bucket (5-7). */
    { "cards",                offsetof(PlayerFile, cardsInjuries),   0x03,             FLD_BITS },
    { "injury",               offsetof(PlayerFile, cardsInjuries),   0x1C,             FLD_BITS },
    { "status_bucket",        offsetof(PlayerFile, cardsInjuries),   0xE0,             FLD_BITS },
    { "passing",              offsetof(PlayerFile, passing),             0, FLD_U8 },
    /* Skill nibble pairs, split into editable halves (hi=first label, lo=second). */
    { "velocity",             offsetof(PlayerFile, velocityHeading),     0, FLD_NIBBLE_HI },
    { "heading",              offsetof(PlayerFile, velocityHeading),     0, FLD_NIBBLE_LO },
    { "tackling",             offsetof(PlayerFile, tacklingControl),     0, FLD_NIBBLE_HI },
    { "control",              offsetof(PlayerFile, tacklingControl),     0, FLD_NIBBLE_LO },
    { "speed",                offsetof(PlayerFile, speedFinishing),      0, FLD_NIBBLE_HI },
    { "finishing",            offsetof(PlayerFile, speedFinishing),      0, FLD_NIBBLE_LO },
    /* Backward-compat (load-only): pre-split saves/data used the packed bytes. */
    { "position_and_face",    offsetof(PlayerFile, positionAndFace),     0, FLD_U8 },
    { "cards_injuries",       offsetof(PlayerFile, cardsInjuries),       0, FLD_U8 },
    { "velocity_heading",     offsetof(PlayerFile, velocityHeading),     0, FLD_U8 },
    { "tackling_control",     offsetof(PlayerFile, tacklingControl),     0, FLD_U8 },
    { "speed_finishing",      offsetof(PlayerFile, speedFinishing),      0, FLD_U8 },
    { "price",                offsetof(PlayerFile, price),               0, FLD_U8 },
    { "goal_stats",           offsetof(PlayerFile, goalStats),           4, FLD_BYTES },
    { "form",                 offsetof(PlayerFile, form),                0, FLD_U8 },
    { NULL, 0, 0, FLD_END }
};

/* TeamFile header field table — keys must match emit_team_file_ex_body
 * inner "team" object. */
static const StructFieldDesc s_team_file_fields[] = {
    { "country_number",     offsetof(TeamFile, countryNumber),     0, FLD_U8 },
    { "team_ordinal",       offsetof(TeamFile, teamOrdinal),       0, FLD_U8 },
    { "global_team_number", offsetof(TeamFile, globalTeamNumber),  0, FLD_S16 },
    { "team_controls",      offsetof(TeamFile, teamControls),      0, FLD_U8 },
    { "team_name",          offsetof(TeamFile, teamName),         17, FLD_STR },
    { "writes_zero_here",   offsetof(TeamFile, writesZeroHere),    0, FLD_U8 },
    { "and_with_0xfe",      offsetof(TeamFile, andWith0xFE),       0, FLD_U8 },
    { "tactics",            offsetof(TeamFile, tactics),           0, FLD_U8 },
    { "league",             offsetof(TeamFile, league),            0, FLD_U8 },
    { "primary_shirt_type",      offsetof(TeamFile, prShirtType),     0, FLD_U8 },
    { "primary_stripes_color",   offsetof(TeamFile, prStripesColor),  0, FLD_U8 },
    { "primary_basic_color",     offsetof(TeamFile, prBasicColor),    0, FLD_U8 },
    { "primary_shorts_color",    offsetof(TeamFile, prShortsColor),   0, FLD_U8 },
    { "primary_socks_color",     offsetof(TeamFile, prSocksColor),    0, FLD_U8 },
    { "secondary_shirt_type",    offsetof(TeamFile, secShirtType),    0, FLD_U8 },
    { "secondary_stripes_color", offsetof(TeamFile, secStripesColor), 0, FLD_U8 },
    { "secondary_basic_color",   offsetof(TeamFile, secBasicColor),   0, FLD_U8 },
    { "secondary_shorts_color",  offsetof(TeamFile, secShortsColor),  0, FLD_U8 },
    { "secondary_socks_color",   offsetof(TeamFile, secSocksColor),   0, FLD_U8 },
    /* Back-compat (load-only): pre-split files used a flat kit_colors[10] array. */
    { "kit_colors",         offsetof(TeamFile, prShirtType),      10, FLD_BYTES },
    { "coach_name",         offsetof(TeamFile, coachName),        23, FLD_STR },
    { "player_values_dirty",offsetof(TeamFile, playerValuesDirty), 0, FLD_U8 },
    { "player_numbers",     offsetof(TeamFile, playerNumbers),    16, FLD_BYTES },
    { NULL, 0, 0, FLD_END }
};

/* TransferOfferEntry field table (transferOfferTable[5]). Promoted from a raw
 * 70-byte FLD_BYTES blob so its host-native uint16/int16 fields round-trip
 * across the LE-SDL <-> BE-Amiga boundary (the int8 ordinals must stay per-byte,
 * which is why a flat FLD_U16_ARRAY can't be used).
 *
 * Unlike the comp-slot / diy-standings overrides (which keep the FLD_BYTES blob
 * and add a PARTIAL typed override because only some fields are sensitive), these
 * field tables give FULL byte coverage, so the blob is removed entirely rather
 * than overridden — no redundant raw bytes. Trade-off: legacy saves don't carry
 * the typed keys, so these fields load empty from an old .CAR (regenerate). */
static const StructFieldDesc s_transfer_offer_fields[] = {
    { "team_offering",        offsetof(TransferOfferEntry, teamOffering),           0, FLD_U16 },
    { "primary_player",       offsetof(TransferOfferEntry, primaryPlayerOrdinal),   0, FLD_S8  },
    { "second_player",        offsetof(TransferOfferEntry, secondPlayerOrdinal),    0, FLD_S8  },
    { "swap_player",          offsetof(TransferOfferEntry, swapPlayerOrdinal),      0, FLD_S8  },
    { "extra_player",         offsetof(TransferOfferEntry, extraPlayerOrdinal),     0, FLD_S8  },
    { "offer_amount",         offsetof(TransferOfferEntry, offerAmount),            0, FLD_U16 },
    { "negotiation_step1",    offsetof(TransferOfferEntry, negotiationStep1Amount), 0, FLD_S16 },
    { "negotiation_step2",    offsetof(TransferOfferEntry, negotiationStep2Amount), 0, FLD_S16 },
    { "lifespan",             offsetof(TransferOfferEntry, lifespan),               0, FLD_U8  },
    { "seen_flag",            offsetof(TransferOfferEntry, seenFlag),               0, FLD_U8  },
    { NULL, 0, 0, FLD_END }
};

/* JobOffer field table (jobOffers[5] / newJobOffers[5]). Promoted from raw
 * 130-byte FLD_BYTES blobs — teamNo (u16), amountOffered (u32) and unkn (u16)
 * are host-native and byte-swapped cross-target as raw bytes. */
static const StructFieldDesc s_job_offer_fields[] = {
    { "team_no",        offsetof(JobOffer, teamNo),        0, FLD_U16   },
    /* teamName uses 0xFF = unset (not a NUL-terminated string), so keep it as
     * raw bytes — endian-safe and byte-exact, unlike FLD_STR. */
    { "team_name",      offsetof(JobOffer, teamName),     18, FLD_BYTES },
    { "amount_offered", offsetof(JobOffer, amountOffered), 0, FLD_U32   },
    { "reserved",       offsetof(JobOffer, reserved),      0, FLD_U16   },
    { NULL, 0, 0, FLD_END }
};

/* Load a PlayerFile from a JSON object slice. Mirror of emit_player_file_body. */
static void load_player_file_body(const char *buf, int buflen, uint8_t *p)
{
    load_struct_object(buf, buflen, p, s_player_fields);
}

/* Emit a single MarketPlayer (42 B = 4 B header + PlayerFile) as a
 * JSON object body. Used by the careerForeignMarketPlayers array. */
static void emit_market_player_body(mjson_print_fn_t fn, void *fnd,
                                    const uint8_t *base)
{
    mjson_printf(fn, fnd, "%s", "{");
    emit_s16_at (fn, fnd, "team_id",        base, offsetof(MarketPlayer, teamId));
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "player_ordinal", base, offsetof(MarketPlayer, playerOrdinal));
    mjson_printf(fn, fnd, "%s", ",");
    emit_s8_at  (fn, fnd, "padding",        base, offsetof(MarketPlayer, padding));
    mjson_printf(fn, fnd, ",%Q:", "player");
    emit_player_file_body(fn, fnd, base + offsetof(MarketPlayer, player));
    mjson_printf(fn, fnd, "%s", "}");
}

/* Load a MarketPlayer from a JSON object slice. Mirror of
 * emit_market_player_body. Called 432 times per career load — the
 * per-field path-lookup version dominated apply_semantic_load at
 * ~38 ms / load. Walk-once dispatch over the four outer keys plus
 * delegation to load_player_file_body for the nested player object. */
static void load_market_player_body(const char *buf, int buflen, uint8_t *base)
{
    int pos = 0, koff, klen, voff, vlen, vt;
    while ((pos = fast_object_next(buf, buflen, pos,
                                   &koff, &klen, &voff, &vlen, &vt)) != 0) {
        if (klen < 2) continue;
        const char *k  = buf + koff + 1;
        int         kl = klen - 2;
        const char *vs = buf + voff;
        int         vl = vlen;

        if (kl == 7 && vt == MJSON_TOK_NUMBER &&
            memcmp(k, "team_id", 7) == 0) {
            int16_t v = (int16_t)parse_long_slice(vs, vl);
            memcpy(base + offsetof(MarketPlayer, teamId), &v, sizeof(v));
        } else if (kl == 14 && vt == MJSON_TOK_NUMBER &&
                   memcmp(k, "player_ordinal", 14) == 0) {
            base[offsetof(MarketPlayer, playerOrdinal)] =
                (uint8_t)parse_long_slice(vs, vl);
        } else if (kl == 7 && vt == MJSON_TOK_NUMBER &&
                   memcmp(k, "padding", 7) == 0) {
            base[offsetof(MarketPlayer, padding)] =
                (uint8_t)parse_long_slice(vs, vl);
        } else if (kl == 6 && vt == MJSON_TOK_OBJECT &&
                   memcmp(k, "player", 6) == 0) {
            load_player_file_body(vs, vl,
                                  base + offsetof(MarketPlayer, player));
        }
    }
}

static void emit_team_file_ex_body(mjson_print_fn_t fn, void *fnd,
                                   const uint8_t *base)
{
    const uint8_t *team = base + offsetof(TeamFileEx, team);
    mjson_printf(fn, fnd, "{%Q:{", "team");
    emit_u8_at  (fn, fnd, "country_number",     team, offsetof(TeamFile, countryNumber));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "team_ordinal",       team, offsetof(TeamFile, teamOrdinal));
    mjson_printf(fn, fnd, "%s", ",");
    emit_s16_at (fn, fnd, "global_team_number", team, offsetof(TeamFile, globalTeamNumber));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "team_controls",      team, offsetof(TeamFile, teamControls));
    mjson_printf(fn, fnd, "%s", ",");
    emit_str_at (fn, fnd, "team_name",          team, offsetof(TeamFile, teamName), 17);
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "writes_zero_here",   team, offsetof(TeamFile, writesZeroHere));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "and_with_0xfe",      team, offsetof(TeamFile, andWith0xFE));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "tactics",            team, offsetof(TeamFile, tactics));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "league",             team, offsetof(TeamFile, league));
    mjson_printf(fn, fnd, "%s", ",");
    /* Kit colors as 10 named fields (primary + secondary, each: shirt_type, stripes,
     * basic, shorts, socks) — clearer to edit than a flat 10-byte array. 2026-06-22. */
    emit_u8_at(fn, fnd, "primary_shirt_type",      team, offsetof(TeamFile, prShirtType));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "primary_stripes_color",   team, offsetof(TeamFile, prStripesColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "primary_basic_color",     team, offsetof(TeamFile, prBasicColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "primary_shorts_color",    team, offsetof(TeamFile, prShortsColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "primary_socks_color",     team, offsetof(TeamFile, prSocksColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "secondary_shirt_type",    team, offsetof(TeamFile, secShirtType));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "secondary_stripes_color", team, offsetof(TeamFile, secStripesColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "secondary_basic_color",   team, offsetof(TeamFile, secBasicColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "secondary_shorts_color",  team, offsetof(TeamFile, secShortsColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at(fn, fnd, "secondary_socks_color",   team, offsetof(TeamFile, secSocksColor));
    mjson_printf(fn, fnd, "%s", ",");
    emit_str_at (fn, fnd, "coach_name",         team, offsetof(TeamFile, coachName), 23);
    mjson_printf(fn, fnd, "%s", ",");
    emit_u8_at  (fn, fnd, "player_values_dirty",team, offsetof(TeamFile, playerValuesDirty));
    mjson_printf(fn, fnd, "%s", ",");
    emit_bytes_at(fn, fnd,"player_numbers",     team, offsetof(TeamFile, playerNumbers), 16);
    mjson_printf(fn, fnd, "%s", "}");

    mjson_printf(fn, fnd, ",%Q:[", "players");
    for (int i = 0; i < 16; i++) {
        const uint8_t *p = base + offsetof(TeamFileEx, players)
                                + (size_t)i * sizeof(PlayerFile);
        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        emit_player_file_body(fn, fnd, p);
    }
    mjson_printf(fn, fnd, "%s", "]}");
}

/* world_playoff's arrays in the career save: numbers by name (include/world_playoff.h). */
static void wp_emit_u16(void *out, const char *name, const uint16_t *a, int n)
{
    mjson_printf(json_out_emit, out, ",%Q:[", name);
    for (int i = 0; i < n; i++)
        mjson_printf(json_out_emit, out, "%s%d", i ? "," : "", (int)a[i]);
    mjson_printf(json_out_emit, out, "%s", "]");
}

static void wp_emit_u8(void *out, const char *name, const uint8_t *a, int n)
{
    mjson_printf(json_out_emit, out, ",%Q:[", name);
    for (int i = 0; i < n; i++)
        mjson_printf(json_out_emit, out, "%s%d", i ? "," : "", (int)a[i]);
    mjson_printf(json_out_emit, out, "%s", "]");
}

static void wp_emit_u32(void *out, const char *name, const uint32_t *a, int n)
{
    mjson_printf(json_out_emit, out, ",%Q:[", name);
    for (int i = 0; i < n; i++)
        mjson_printf(json_out_emit, out, "%s%lu", i ? "," : "", (unsigned long)a[i]);
    mjson_printf(json_out_emit, out, "%s", "]");
}

static int wp_load_u32(const char *o, int ol, const char *path, uint32_t *a, int cap)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0;
    long v;                             /* (the values fit 31 bits: the Amiga's mjson has no doubles) */
    if (mjson_find(o, ol, path, &arr, &al) == MJSON_TOK_ARRAY)
        while (i < cap && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
            if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                a[i++] = (uint32_t)v;
    return i;
}

static int wp_load_u16(const char *o, int ol, const char *path, uint16_t *a, int cap)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0;
    long v;
    if (mjson_find(o, ol, path, &arr, &al) == MJSON_TOK_ARRAY)
        while (i < cap && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
            if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                a[i++] = (uint16_t)v;
    return i;
}

static int wp_load_u8(const char *o, int ol, const char *path, uint8_t *a, int cap)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0;
    long v;
    if (mjson_find(o, ol, path, &arr, &al) == MJSON_TOK_ARRAY)
        while (i < cap && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
            if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                a[i++] = (uint8_t)v;
    return i;
}

static void emit_team_file_ex(mjson_print_fn_t fn, void *fnd,
                              const char *key, const uint8_t *base)
{
    mjson_printf(fn, fnd, "%Q:", key);
    emit_team_file_ex_body(fn, fnd, base);
}

/* Resolve a path against `buf` and run `body` with `slice`/`slice_len`
 * set to the matched sub-object. Skips body if not found / not an
 * object. Used by the optimised loaders to bound mjson_get_* scans
 * to a small sub-buffer instead of the whole file. */
#define WITH_OBJECT_SLICE(buf, buflen, path_str, slice_var, slicelen_var, body) \
    do { \
        const char *slice_var = NULL; int slicelen_var = 0; \
        if (mjson_find((buf), (buflen), (path_str), \
                       &slice_var, &slicelen_var) == MJSON_TOK_OBJECT) { \
            body \
        } \
    } while (0)

/* Old signature kept for callers that prefer to pass a path against
 * the full file; internally it slices then dispatches to the fast
 * path. */
static void load_team_file_ex_slice(const char *buf, int buflen,
                                    uint8_t *base);

static void load_team_file_ex(const char *buf, int buflen,
                              const char *path_prefix, uint8_t *base)
{
    WITH_OBJECT_SLICE(buf, buflen, path_prefix, slice, slice_len, {
        load_team_file_ex_slice(slice, slice_len, base);
    });
}

/* Fast variant: `buf`/`buflen` is already the TeamFileEx sub-object's
 * JSON slice. mjson_get_* calls below scan ~3 KB instead of the full
 * .CAR file — typically 100× faster for the selected_teams loop. */
static void load_team_file_ex_slice(const char *buf, int buflen,
                                    uint8_t *base)
{
    uint8_t *team = base + offsetof(TeamFileEx, team);

    /* Walk the outer { "team": {...}, "players": [...] } object once
     * via mjson_next. Each top-level key is dispatched directly to a
     * single sub-walker — replaces the per-field mjson_get_long calls
     * that each scanned the full outer slice. The two top-level keys
     * are short so a 2-way string compare is fastest. */
    int pos = 0, koff, klen, voff, vlen, vt;
    while ((pos = fast_object_next(buf, buflen, pos,
                                   &koff, &klen, &voff, &vlen, &vt)) != 0) {
        if (klen < 2) continue;
        const char *k = buf + koff + 1;
        int         kl = klen - 2;
        const char *vs = buf + voff;
        int         vl = vlen;

        if (kl == 4 && vt == MJSON_TOK_OBJECT &&
            memcmp(k, "team", 4) == 0) {
            load_struct_object(vs, vl, team, s_team_file_fields);
        } else if (kl == 7 && vt == MJSON_TOK_ARRAY &&
                   memcmp(k, "players", 7) == 0) {
            int pos2 = 0, voff2, vlen2, vt2;
            int i = 0;
            while (i < 16 &&
                   (pos2 = fast_array_next(vs, vl, pos2,
                                           &voff2, &vlen2, &vt2)) != 0) {
                if (vt2 != MJSON_TOK_OBJECT) { i++; continue; }
                uint8_t *p = base + offsetof(TeamFileEx, players)
                                  + (size_t)i * sizeof(PlayerFile);
                load_player_file_body(vs + voff2, vlen2, p);
                i++;
            }
        }
    }
}

/* ======================================================================
 * Data-file JSON loaders (bin/data_json) — docs/DATA_INTERNAL_JSON.md
 *
 * These reuse the save-format machinery above (load_team_file_ex_slice /
 * load_struct_object / the s_*_fields tables) so the bin/data JSON and the
 * .CAR JSON share ONE representation and ONE loader. Phase 4 of the data
 * jailbreak; teams first.
 * ====================================================================== */

/* Load a teams/TMD JSON document — { ..., "teams":[ {team,players}, ... ] } —
 * into out[] (each a TeamFileEx). Returns the count loaded, or -1 if there is
 * no teams array. The same shape covers TEAM.<country>.json and the *.TMD.json. */
int DataJson_LoadTeamArray(const char *json, int len, struct TeamFileEx *out, int max)
{
    const char *arr;
    int arr_len;
    if (mjson_find(json, len, "$.teams", &arr, &arr_len) != MJSON_TOK_ARRAY)
        return -1;
    int pos = 0, voff, vlen, vt, i = 0;
    while (i < max && (pos = fast_array_next(arr, arr_len, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT)
            continue;
        memset(&out[i], 0, sizeof(TeamFileEx));
        load_team_file_ex_slice(arr + voff, vlen, (uint8_t *)&out[i]);
        i++;
    }
    return i;
}

#ifdef SWOS_SDL_BUILD
#include "team_db.h"        /* struct TeamDbIds: the ids a teams file states */
/* Read a whole file into a malloc'd NUL-terminated buffer (*out_len = byte len).
   Caller frees. Returns NULL on any error. */
static char *dj_slurp(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = '\0';
    fclose(f);
    *out_len = (int)n;
    return buf;
}

#include "career_params.h"
/* Load bin/data_json/career_params.json (if present) over the compiled defaults — lets
 * testers tune career-mode economy + board expectations without a rebuild. A missing file
 * or a missing field leaves the compiled default in place, so behaviour is unchanged by
 * default. */
void DataJson_LoadCareerParams(void)
{
    int len;
    char *j = dj_slurp("data_json/career_params.json", &len);
    if (!j)
        return;
    double v;
    #define LDU(path, f) do { if (mjson_get_number(j, len, path, &v)) _careerParams.f = (uint16_t)v; } while (0)
    #define LDS(path, f) do { if (mjson_get_number(j, len, path, &v)) _careerParams.f = (int16_t)v;  } while (0)
    LDU("$.start_balance_value_mult",   start_balance_value_mult);
    LDU("$.start_balance_pct_min",      start_balance_pct_min);
    LDU("$.start_balance_pct_range",    start_balance_pct_range);
    LDU("$.start_balance_scale",        start_balance_scale);
    LDU("$.gate_value_divisor",         gate_value_divisor);
    LDU("$.gate_league_mult_home",      gate_league_mult_home);
    LDU("$.gate_league_mult_away",      gate_league_mult_away);
    LDU("$.gate_cup_mult",              gate_cup_mult);
    LDU("$.wage_trialist_pct",          wage_trialist_pct);
    LDU("$.wage_reserve_pct",           wage_reserve_pct);
    LDU("$.wage_scale",                 wage_scale);
    LDS("$.board_ratio_ok",             board_ratio_ok);
    LDS("$.board_ratio_good",           board_ratio_good);
    LDS("$.board_ratio_poor",           board_ratio_poor);
    LDS("$.board_ratio_terrible",       board_ratio_terrible);
    LDU("$.board_percentile_low",       board_percentile_low);
    LDU("$.board_percentile_high",      board_percentile_high);
    LDU("$.board_secondary_top2_bonus", board_secondary_top2_bonus);
    LDU("$.board_tertiary_top2_bonus",  board_tertiary_top2_bonus);
    LDU("$.board_euro_top2_bonus",      board_euro_top2_bonus);
    LDU("$.num_seasons",                num_seasons);
    #undef LDU
    #undef LDS
    if (_careerParams.num_seasons < 1) _careerParams.num_seasons = 1;
    if (_careerParams.num_seasons > CAREER_MAX_SEASONS) _careerParams.num_seasons = CAREER_MAX_SEASONS;
    free(j);
    fprintf(stderr, "[data-json] career params loaded from data_json/career_params.json\n");
}

/* Slurp `path`, find the record whose "$.id" == id inside the array at
   "$.<arrkey>", and return its object slice. On success returns 1, sets
   *filebuf to the malloc'd file (caller must free it once done with the slice)
   and *o/*ol to the record slice. Returns 0 (and frees the file) otherwise. */
/* A world's data (include/world.h) sits in data_json/worlds/<tag>/ beside 96/97's, file
 * for file -- teams/ITALY.json, competitions/leagues.json -- holding only what differs:
 * a competition record by its id, a country's team file whole. `path` is 96/97's
 * ("data_json/..."); the world's is written to `out`. 0 for 96/97, which has none. */
static int dj_world_path(const char *path, char *out, size_t out_size)
{
    const char *rel = strncmp(path, "data_json/", 10) == 0 ? path + 10 : path;
    if (World_Current() == WORLD_9697)
        return 0;
    snprintf(out, out_size, "data_json/worlds/%s/%s", World_Tag(), rel);
    return 1;
}

/* The record `id` of one file, as a slice of `fb` (or 0). */
static int dj_record_in(char *fb, int len, const char *arrkey, const char *id,
                        const char **o, int *ol)
{
    const char *arr; int al;
    char apath[64];
    snprintf(apath, sizeof apath, "$.%s", arrkey);
    if (mjson_find(fb, len, apath, &arr, &al) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt; char idbuf[64];
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            if (mjson_get_string(arr + voff, vlen, "$.id", idbuf, (int)sizeof idbuf) > 0
                && strcmp(idbuf, id) == 0) {
                *o = arr + voff; *ol = vlen;
                return 1;
            }
        }
    }
    return 0;
}

static int dj_find_comp_record(const char *path, const char *arrkey, const char *id,
                               char **filebuf, const char **o, int *ol)
{
    int len = 0;
    char wpath[256];
    /* The world's record first (an overlay of the records that differ from 96/97's). */
    if (dj_world_path(path, wpath, sizeof wpath)) {
        char *wfb = dj_slurp(wpath, &len);
        if (wfb) {
            if (dj_record_in(wfb, len, arrkey, id, o, ol)) {
                *filebuf = wfb;
                return 1;
            }
            free(wfb);
        }
    }
    char *fb = dj_slurp(path, &len);
    if (!fb)
        return 0;
    const char *arr; int al;
    char apath[64];
    snprintf(apath, sizeof apath, "$.%s", arrkey);
    if (mjson_find(fb, len, apath, &arr, &al) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt; char idbuf[64];
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            if (mjson_get_string(arr + voff, vlen, "$.id", idbuf, (int)sizeof idbuf) > 0
                && strcmp(idbuf, id) == 0) {
                *filebuf = fb; *o = arr + voff; *ol = vlen;
                return 1;
            }
        }
    }
    free(fb);
    return 0;
}

/* SWOS_DATA_JSON wire-in: load a country's teams from data_json/ instead of the
   binary TEAM.NNN. Resolves countryNum -> "teams/NAME.json" via the master
   data_json/competitions/countries.json list, reads it, and parses it into
   `teamFileBuf` in the exact layout LoadTeamFile expects: a 2-byte HOST-NATIVE
   count header followed by `count` TeamFileEx records. Returns the team count
   (>0) on success, or -1 to tell the caller to FALL BACK to the binary file
   (country not in the JSON corpus, file missing, or parse failure). No byteswap
   is needed afterwards — fields are parsed as logical host-native ints. */
/* The teams file of a country, read whole (caller frees), with its name under
   data_json/ in `team_file`. NULL if the country is not in the JSON corpus. */
static char *dj_slurp_country_teams(int countryNum, char *team_file, int team_file_size, int *out_len)
{
    int clen = 0;
    char *cj = dj_slurp("data_json/competitions/countries.json", &clen);
    if (!cj)
        return NULL;

    team_file[0] = '\0';
    const char *arr;
    int arr_len;
    if (mjson_find(cj, clen, "$.countries", &arr, &arr_len) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt;
        while ((pos = fast_array_next(arr, arr_len, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT)
                continue;
            double num = -1;
            if (mjson_get_number(arr + voff, vlen, "$.number", &num) &&
                (int)num == countryNum) {
                mjson_get_string(arr + voff, vlen, "$.team_file",
                                 team_file, team_file_size);
                break;
            }
        }
    }
    free(cj);
    if (!team_file[0] && World_CountryTeamFile((uint8_t)countryNum)) {
        /* A country new to the game: the world's file alone (include/world.h). */
        char wpath[256];
        snprintf(team_file, team_file_size, "%s", World_CountryTeamFile((uint8_t)countryNum));
        snprintf(wpath, sizeof wpath, "data_json/worlds/%s/%s", World_Tag(), team_file);
        return dj_slurp(wpath, out_len);
    }
    if (!team_file[0])
        return NULL;   /* country not represented in the JSON corpus */

    char path[256], wpath[256];
    snprintf(path, sizeof path, "data_json/%s", team_file);
    /* The world's file for this country, whole, if it has one (include/world.h). */
    if (dj_world_path(path, wpath, sizeof wpath)) {
        char *w = dj_slurp(wpath, out_len);
        if (w)
            return w;
    }
    return dj_slurp(path, out_len);
}

/* A world's own team file by name (teams/<name> of the world: an off-file pool,
   src/team_pool.c), into teamFileBuf as DataJson_LoadTeamFileForCountry does. */
int DataJson_LoadWorldTeamFile(const char *name, unsigned char *teamFileBuf, int maxTeams)
{
    char path[256], wpath[256];
    int jlen = 0;
    snprintf(path, sizeof path, "data_json/teams/%s", name);
    if (!dj_world_path(path, wpath, sizeof wpath))
        return -1;
    char *jj = dj_slurp(wpath, &jlen);
    if (!jj)
        return -1;
    int n = DataJson_LoadTeamArray(jj, jlen, (struct TeamFileEx *)(teamFileBuf + 2), maxTeams);
    free(jj);
    if (n <= 0)
        return -1;
    *(unsigned short *)teamFileBuf = (unsigned short)n;
    return n;
}

/* The ids a country's teams file states (docs/TEAM_IDS.md): for each team of its
   "teams" array, in order -- the same order DataJson_LoadTeamFileForCountry gives the
   records -- its "uuid", its "checksum" and each player's "uuid"; "" for one that is
   not there. Returns the team count, or -1 as that function does. */
int DataJson_LoadTeamIdsForCountry(int countryNum, struct TeamDbIds *out, int maxTeams)
{
    char team_file[128];
    int jlen = 0;
    char *jj = dj_slurp_country_teams(countryNum, team_file, (int)sizeof team_file, &jlen);
    if (!jj)
        return -1;

    const char *arr;
    int arr_len, n = 0;
    if (mjson_find(jj, jlen, "$.teams", &arr, &arr_len) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt;
        while (n < maxTeams && (pos = fast_array_next(arr, arr_len, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT)
                continue;
            struct TeamDbIds *ids = &out[n++];
            const char *players;
            int players_len;
            memset(ids, 0, sizeof *ids);
            mjson_get_string(arr + voff, vlen, "$.uuid", ids->uuid, (int)sizeof ids->uuid);
            mjson_get_string(arr + voff, vlen, "$.checksum", ids->checksum, (int)sizeof ids->checksum);
            if (mjson_find(arr + voff, vlen, "$.players", &players, &players_len) == MJSON_TOK_ARRAY) {
                int pos2 = 0, voff2, vlen2, vt2, p = 0;
                while (p < TEAM_PLAYERS &&
                       (pos2 = fast_array_next(players, players_len, pos2, &voff2, &vlen2, &vt2)) != 0) {
                    if (vt2 == MJSON_TOK_OBJECT)
                        mjson_get_string(players + voff2, vlen2, "$.uuid",
                                         ids->player[p], (int)sizeof ids->player[p]);
                    p++;
                }
            }
        }
    }
    free(jj);
    return n > 0 ? n : -1;
}

int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams)
{
    char team_file[128];
    int jlen = 0;
    char *jj = dj_slurp_country_teams(countryNum, team_file, (int)sizeof team_file, &jlen);
    if (!jj)
        return -1;

    int n = DataJson_LoadTeamArray(jj, jlen,
                                   (struct TeamFileEx *)(teamFileBuf + 2), maxTeams);
    free(jj);
    if (n <= 0)
        return -1;
    *(unsigned short *)teamFileBuf = (unsigned short)n;   /* host-native count header */
    {
        /* Announce ONCE so a test run can confirm the JSON path is live (not a
           silent fallback to the binary TEAM.NNN). */
        static int announced = 0;
        if (!announced) {
            announced = 1;
            fprintf(stderr, "[data-json] TEAM LOADING ACTIVE — country %d <- data_json/%s (%d teams)\n",
                    countryNum, team_file, n);
            fflush(stderr);
        }
    }
    return n;
}

/* ----------------------------------------------------------------------------
 * Competition JSON loader (data_json/competitions/*.json) — first increment:
 * parse a single competition RECORD (the cup / playoff family: comp base +
 * scalars + round_rule_flags + name strings + team_seed_list) into the typed
 * CompetitionCup struct. Tournament rounds[]/league divisions[] and the
 * country-graph wire-in (competitionsTable reconstruction) are the next steps.
 * -------------------------------------------------------------------------- */

/* CN_* define -> country number, via the master countries.json (file cached for
   the process; linear scan per lookup — fine for load-time use). Returns -1. */
static int dj_cn_number(const char *define, int dlen)
{
    static char *cj = (char *)0;
    static int clen = 0;
    static int tried = 0;
    if (!tried) { tried = 1; cj = dj_slurp("data_json/competitions/countries.json", &clen); }
    if (!cj) return -1;
    const char *arr; int al;
    if (mjson_find(cj, clen, "$.countries", &arr, &al) != MJSON_TOK_ARRAY) return -1;
    int pos = 0, voff, vlen, vt;
    char buf[64];
    while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) continue;
        if (mjson_get_string(arr + voff, vlen, "$.define", buf, (int)sizeof buf) > 0
            && (int)strlen(buf) == dlen && memcmp(buf, define, (size_t)dlen) == 0) {
            double v;
            return mjson_get_number(arr + voff, vlen, "$.number", &v) ? (int)v : -1;
        }
    }
    return -1;
}

/* ---- shared parse helpers for the competition-record loaders ----
 * The three concrete competition records (CompetitionCup/Playoffs share one,
 * CompetitionTournament, CompetitionLeague) all embed a Competition base, an
 * interned name[] table, and (cup/tournament) a TeamId seed list, so those
 * sub-parsers are factored out here and shared by all loaders. */

/* Parse the "$.comp" base sub-object into c. isPlayoff sets the byte-6
   discriminator (the caller knows it from the source list); the two inert
   self-offset bytes (reserved1/reserved2) are zeroed — they are no longer read
   after Step 4b. Returns 0, or -1 if the comp base object is missing. */
static int dj_comp_base(const char *o, int ol, Competition *c, int isPlayoff)
{
    const char *cs; int cl; double v;
    if (mjson_find(o, ol, "$.comp", &cs, &cl) != MJSON_TOK_OBJECT) return -1;
#define GN(s, sl, p) (mjson_get_number((s), (sl), (p), &v) ? (int)v : 0)
    c->compId      = (uint8_t)GN(cs, cl, "$.comp_id");
    c->type            = (int8_t)GN(cs, cl, "$.type");
    c->teamNumber      = (int8_t)GN(cs, cl, "$.team_number");
    c->beginMonth      = (int8_t)GN(cs, cl, "$.begin_month");
    c->endMonth        = (int8_t)GN(cs, cl, "$.end_month");
    /* byte5/byte7: the old nameTableOffset / teamListOffset. Step 4b reaches the
       name/seed tables via comp_name_table()/comp_team_seed_list() instead, so
       these are "inert" by design — BUT a record copy that differs from the
       compiled global by even one byte trips the season-transition scratch-buffer
       checksum (DIY_ValidateLeagueScratchBuffer, panic 0x4444). Keep them
       byte-faithful: parse the stored values rather than zeroing. */
    c->reserved1       = (int8_t)GN(cs, cl, "$.name_table_offset");
    c->isPlayoff       = (int8_t)isPlayoff;   /* byte 6 discriminator (Step 4b) */
    c->reserved2       = (int8_t)GN(cs, cl, "$.team_list_offset");
    c->seedReorderFlag = (int8_t)GN(cs, cl, "$.seed_reorder_flag");
#undef GN
    return 0;
}

/* Intern up to `max` JSON strings from the array at `path` into strpool,
   writing pointers to out[]. Missing/over-long entries stay as-is (caller
   memset the record to 0, so they read NULL = "use default name"). */
static void dj_intern_names(const char *o, int ol, const char *path,
                            char **out, int max, char *strpool, int strpoolsz)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0, used = 0;
    if (mjson_find(o, ol, path, &arr, &al) != MJSON_TOK_ARRAY) return;
    while (i < max && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
        if (vt == MJSON_TOK_STRING) {
            /* The JSON now carries the string SYMBOL name (e.g. "aDivision1");
             * localize it via tr() for the active language. Falls back to the
             * symbol name itself if translations aren't loaded / the symbol is
             * unknown (keeps the slot non-NULL and debuggable). */
            char sym[64];
            int n = mjson_get_string(arr + voff, vlen, "$", sym, (int)sizeof sym);
            if (n > 0) {
                const char *txt = tr(sym);
                if (txt) {
                    out[i] = (char *)txt;                         /* localized (translation pool) */
                } else {
                    /* No translation loaded/found: fall back to the COMPILED string
                     * the symbol NAMES (draw-time tr_ptr can still localize that
                     * address), NOT the raw symbol key. Otherwise every data-driven
                     * competition name renders as its key ("_ASERIEA", "_A1LIGA", …)
                     * on any build where the translation table isn't populated
                     * (observed on the macOS app). Matches translations.h's
                     * "fall back to the compiled string symbol" contract. */
                    const void *addr = Translations_SymbolAddr(sym);
                    if (addr) {
                        out[i] = (char *)addr;                    /* compiled string symbol */
                    } else {
                        /* Last resort: the literal name -- a world's own division names
                         * ("SERIE D GIRONE A"), each usually twice (full name and
                         * abbreviation): one copy in the pool serves both. */
                        for (int k = 0; k < i; k++)
                            if (out[k] && out[k] >= strpool && out[k] < strpool + used &&
                                strcmp(out[k], sym) == 0) {
                                out[i] = out[k];
                                break;
                            }
                        if (!out[i] && used + n + 1 <= strpoolsz) {
                            memcpy(strpool + used, sym, (size_t)n + 1);
                            out[i] = strpool + used; used += n + 1;
                        }
                    }
                }
            }
        }
        i++;
    }
}

/* Parse "$.team_seed_list" ({country: "CN_*", ordinal}) into out[max] TeamIds. */
static void dj_seed_list(const char *o, int ol, TeamId *out, int max)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0; double v; char cn[64];
    if (mjson_find(o, ol, "$.team_seed_list", &arr, &al) != MJSON_TOK_ARRAY) return;
    while (i < max && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) continue;
        int num = -1;
        if (mjson_get_string(arr + voff, vlen, "$.country", cn, (int)sizeof cn) > 0)
            num = dj_cn_number(cn, (int)strlen(cn));
        out[i].countryNumber = (uint8_t)(num < 0 ? 0 : num);
        out[i].teamOrdinal   = (uint8_t)(mjson_get_number(arr + voff, vlen, "$.ordinal", &v) ? (int)v : 0);
        i++;
    }
}

/* Parse a flat numeric array at `path` into out[max] int8 bytes (values may be
   negative, e.g. -1 sentinels — stored as the same bit pattern). */
static void dj_i8_array(const char *o, int ol, const char *path, int8_t *out, int max)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0; double v;
    if (mjson_find(o, ol, path, &arr, &al) != MJSON_TOK_ARRAY) return;
    while (i < max && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
        out[i++] = (int8_t)(mjson_get_number(arr + voff, vlen, "$", &v) ? (int)v : 0);
}

/* Read round_rule_flags: each element is either the editable object
 * { legs, extra_time, penalties, low? } — bits 7-6 / 5-4 / 3-2 / 1-0 — or a plain packed
 * byte (back-compat). Recombine into the packed per-round rule byte. */
static void dj_round_rules(const char *o, int ol, const char *path, int8_t *out, int max)
{
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0; double v;
    if (mjson_find(o, ol, path, &arr, &al) != MJSON_TOK_ARRAY) return;
    while (i < max && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
        const char *e = arr + voff; int el = vlen;
        if (vt == MJSON_TOK_OBJECT) {
            int legs = mjson_get_number(e, el, "$.legs",       &v) ? (int)v : 0;
            int et   = mjson_get_number(e, el, "$.extra_time", &v) ? (int)v : 0;
            int pen  = mjson_get_number(e, el, "$.penalties",  &v) ? (int)v : 0;
            int low  = mjson_get_number(e, el, "$.low",        &v) ? (int)v : 0;
            out[i++] = (int8_t)(((legs & 3) << 6) | ((et & 3) << 4) | ((pen & 3) << 2) | (low & 3));
        } else {
            out[i++] = (int8_t)(mjson_get_number(e, el, "$", &v) ? (int)v : 0);   /* back-compat */
        }
    }
}

/* Parse one cup/playoff competition record (`o`/`ol` = the JSON object slice)
   into `out`. isPlayoff selects the Competition.isPlayoff discriminator (the
   caller knows it from the source list). name[] strings are interned into the
   caller-provided strpool. Returns 0 on success, -1 if the `comp` base is
   missing. */
/* Recombine the split substitutes (hi nibble) + low-nibble companion (points_for_win
 * for cups/tournaments, max_subs for leagues/playoffs) into the packed byte. Falls back
 * to the old packed key (subs_and_points_for_win / subs_and_max_subs) for pre-split files. */
static int dj_subs_pair(const char *o, int ol, const char *lo_path, const char *packed_path)
{
    double v;
    if (mjson_get_number(o, ol, "$.substitutes", &v)) {
        int subs = (int)v;
        int lo   = mjson_get_number(o, ol, lo_path, &v) ? (int)v : 0;
        return (int)(((subs & 0x0F) << 4) | (lo & 0x0F));
    }
    return mjson_get_number(o, ol, packed_path, &v) ? (int)v : 0;
}

int DataJson_LoadCompetitionCup(const char *o, int ol, void *outv,
                                int isPlayoff, char *strpool, int strpoolsz)
{
    CompetitionCup *out = (CompetitionCup *)outv;
    memset(out, 0, sizeof *out);
    if (dj_comp_base(o, ol, &out->header.comp, isPlayoff) != 0) return -1;
    double v;
#define GN(s, sl, p) (mjson_get_number((s), (sl), (p), &v) ? (int)v : 0)
    out->header.numRoundsOverride   = (int8_t)GN(o, ol, "$.num_rounds_override");
    out->header.teams               = (int8_t)GN(o, ol, "$.teams");
    out->header.awayGoalsMode       = (int8_t)GN(o, ol, "$.away_goals_mode");
    out->header.subsAndPointsForWin = (int8_t)dj_subs_pair(o, ol, "$.points_for_win", "$.subs_and_points_for_win");
    out->header.roundStringBase     = (int8_t)GN(o, ol, "$.round_string_base");
#undef GN
    dj_round_rules(o, ol, "$.round_rule_flags", out->roundRuleFlags, 6);
    dj_intern_names(o, ol, "$.name", out->name, 2, strpool, strpoolsz);
    dj_seed_list(o, ol, out->teamSeedList, 32);
    return 0;
}

/* Parse one promotion-playoff record into `out` (Playoffs struct). The Playoffs
   layout differs from CompetitionCup — roundRuleFlags is [2] (not [6]), there's
   no embedded teamSeedList, and the editor bytes (numRoundsOverride / teams /
   awayGoalsMode / subsAndMaxSubs / roundStringBase) sit between the header and
   name[2]. qualifiedTeamIds[8] is runtime-filled (left zero by the memset).
   Verified field-for-field against the compiled div1Playoffs initializer. */
int DataJson_LoadPlayoffs(const char *o, int ol, void *outv,
                          char *strpool, int strpoolsz)
{
    Playoffs *out = (Playoffs *)outv;
    memset(out, 0, sizeof *out);
    if (dj_comp_base(o, ol, &out->comp, 1) != 0) return -1;   /* isPlayoff = 1 */
    double v;
#define GN(s, sl, p) (mjson_get_number((s), (sl), (p), &v) ? (int)v : 0)
    out->numRoundsOverride = (int8_t)GN(o, ol, "$.num_rounds_override");
    out->teams             = (int8_t)GN(o, ol, "$.teams");
    out->awayGoalsMode     = (int8_t)GN(o, ol, "$.away_goals_mode");
    out->subsAndMaxSubs    = (int8_t)dj_subs_pair(o, ol, "$.max_subs", "$.subs_and_max_subs");
    out->roundStringBase   = (int8_t)GN(o, ol, "$.round_string_base");
#undef GN
    dj_round_rules(o, ol, "$.round_rule_flags", out->roundRuleFlags, 2);
    dj_intern_names(o, ol, "$.name", out->name, 2, strpool, strpoolsz);
    return 0;
}

/* Parse one type-2 group-tournament record into `out` (CompetitionTournament). */
int DataJson_LoadCompetitionTournament(const char *o, int ol, void *outv,
                                       char *strpool, int strpoolsz)
{
    CompetitionTournament *out = (CompetitionTournament *)outv;
    memset(out, 0, sizeof *out);
    if (dj_comp_base(o, ol, &out->header.comp, 0) != 0) return -1;
    double v;
#define GN(s, sl, p) (mjson_get_number((s), (sl), (p), &v) ? (int)v : 0)
    out->header.seedingType         = (int8_t)GN(o, ol, "$.seeding_type");
    out->header.menuState           = (int8_t)GN(o, ol, "$.menu_state");
    out->header.playEachTeam        = (int8_t)GN(o, ol, "$.play_each_team");
    out->header.awayGoalsMode       = (int8_t)GN(o, ol, "$.away_goals_mode");
    out->header.subsAndPointsForWin = (int8_t)dj_subs_pair(o, ol, "$.points_for_win", "$.subs_and_points_for_win");
    out->header.roundStringBase     = (int8_t)GN(o, ol, "$.round_string_base");
    out->header.reserved            = (int8_t)GN(o, ol, "$.header_reserved");
    /* rounds[6]: {team_count, group_count, team_group_count} */
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0;
    if (mjson_find(o, ol, "$.rounds", &arr, &al) == MJSON_TOK_ARRAY) {
        while (i < 6 && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt == MJSON_TOK_OBJECT) {
                out->header.rounds[i].teamCount      = (int8_t)GN(arr + voff, vlen, "$.team_count");
                out->header.rounds[i].groupCount     = (int8_t)GN(arr + voff, vlen, "$.group_count");
                out->header.rounds[i].teamGroupCount = (int8_t)GN(arr + voff, vlen, "$.team_group_count");
            }
            i++;
        }
    }
#undef GN
    dj_round_rules(o, ol, "$.round_rule_flags", out->roundRuleFlags, 6);
    dj_intern_names(o, ol, "$.name", out->name, 2, strpool, strpoolsz);
    dj_seed_list(o, ol, out->teamSeedList, 32);
    return 0;
}

/* Parse one type-0 league record into `out` (CompetitionLeague). The cross-ref
   playoffLinkPtrs[] (JSON "playoff_links": [comp-id, ...]) are NOT resolved here
   — they point at other competition globals and are wired up at the
   country-graph stage; left NULL. promotionLinkData[24] is a plain byte array
   and is parsed now. */
int DataJson_LoadCompetitionLeague(const char *o, int ol, void *outv,
                                   char *strpool, int strpoolsz)
{
    CompetitionLeague *out = (CompetitionLeague *)outv;
    memset(out, 0, sizeof *out);
    if (dj_comp_base(o, ol, &out->header.comp, 0) != 0) return -1;
    double v;
#define GN(s, sl, p) (mjson_get_number((s), (sl), (p), &v) ? (int)v : 0)
    out->header.divisionsCount = (int8_t)GN(o, ol, "$.divisions_count");
    out->header.rounds         = (int8_t)GN(o, ol, "$.rounds");
    out->header.pointsForWin   = (int8_t)GN(o, ol, "$.points_for_win");
    out->header.subsAndMaxSubs = (int8_t)dj_subs_pair(o, ol, "$.max_subs", "$.subs_and_max_subs");
    out->reserved1             = (uint8_t)GN(o, ol, "$.reserved1");
    /* divisions[LEAGUE_MAX_DIVISIONS] */
    const char *arr; int al, pos = 0, voff, vlen, vt, i = 0;
    if (mjson_find(o, ol, "$.divisions", &arr, &al) == MJSON_TOK_ARRAY) {
        while (i < LEAGUE_MAX_DIVISIONS && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt == MJSON_TOK_OBJECT) {
                out->divisions[i].teams               = (int8_t)GN(arr + voff, vlen, "$.teams");
                out->divisions[i].promoted            = (int8_t)GN(arr + voff, vlen, "$.promoted");
                out->divisions[i].promotedPo          = (int8_t)GN(arr + voff, vlen, "$.promoted_po");
                out->divisions[i].relegated           = (int8_t)GN(arr + voff, vlen, "$.relegated");
                out->divisions[i].relegatedPo         = (int8_t)GN(arr + voff, vlen, "$.relegated_po");
                out->divisions[i].qualifierLinkOffset = (int8_t)GN(arr + voff, vlen, "$.qualifier_link_offset");
            }
            i++;
        }
    }
#undef GN
    dj_intern_names(o, ol, "$.name", out->name, 2 * LEAGUE_MAX_DIVISIONS, strpool, strpoolsz);
    dj_i8_array(o, ol, "$.promotion_link_data", (int8_t *)out->promotionLinkData, 24);
    return 0;
}

/* ======================================================================
 * Runtime country-graph wire-in (SWOS_DATA_JSON)
 *
 * Rebuilds the per-country competition tables in competitionsTable[] from the
 * data_json/ corpus at startup, so the ~65 "normal" countries (those carrying
 * leagues[]+cups[] in country_competitions.json) run off the JSON instead of the
 * compiled competition_data.c tables. Region/special tables (europe/world/
 * customs — QualifyingGroup structs) keep their compiled pointers, as do all the
 * named competition globals (englishLeague &c.) that the career playoff /
 * qualification code reads directly by name. Gated on SWOS_DATA_JSON; a country
 * whose ids don't all resolve is left compiled (never a partial table).
 *
 * The validation in dj_selftest_graph() proves this reconstruction is faithful
 * (membership/order/compNumbers match the compiled tables) before it goes live.
 * ====================================================================== */
#define DJ_MAX_CUPS    96
#define DJ_MAX_LEAGUES 80
#define DJ_MAX_RECS    280   /* name-pool slices: ~142 parsed + ~14 complex-country
                                in-place + 20 standalone tournament/playoff in-place +
                                26 special-comp in-place (region/career cups + customs) */
#define DJ_NAMEPOOL    512   /* a world's league: 14 divisions of its own names */
#define DJ_MAX_TABLES  72
#define DJ_TABLE_SLOTS 16

/* Persistent storage — competitionsTable entries point into these, so they must
   outlive startup (static, never freed). */
static CompetitionCup    s_djCups[DJ_MAX_CUPS];
static CompetitionLeague s_djLeagues[DJ_MAX_LEAGUES];
static char              s_djNamePool[DJ_MAX_RECS][DJ_NAMEPOOL];
static Competition      *s_djCountryTables[DJ_MAX_TABLES][DJ_TABLE_SLOTS];

struct dj_graph_reg { char id[40]; Competition *comp; int hasPlayoffLinks; };

/* Parse every record of one corpus file into persistent storage `dst`
   (element size elemsz, capacity cap, running count *n), recording
   id -> &dst[i] in the registry. isLeague picks the loader. */
static void dj_graph_parse_file(const char *path, const char *arrkey, int isLeague,
                                void *dst, size_t elemsz, int cap, int *n,
                                struct dj_graph_reg *reg, int *regN, int regMax,
                                int *poolIdx)
{
    int len = 0, wlen = 0;
    char *fb = dj_slurp(path, &len);
    if (!fb) return;
    /* The world's overlay: a record of the same id replaces 96/97's (include/world.h). */
    char wpath[256];
    char *wfb = dj_world_path(path, wpath, sizeof wpath) ? dj_slurp(wpath, &wlen) : NULL;
    const char *arr; int al; char apath[64];
    snprintf(apath, sizeof apath, "$.%s", arrkey);
    if (mjson_find(fb, len, apath, &arr, &al) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt;
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            char idbuf[40];
            if (mjson_get_string(arr + voff, vlen, "$.id", idbuf, (int)sizeof idbuf) <= 0) continue;
            if (*n >= cap || *poolIdx >= DJ_MAX_RECS || *regN >= regMax) break;
            void *slot = (char *)dst + (size_t)(*n) * elemsz;
            char *pool = s_djNamePool[*poolIdx];
            const char *rec = arr + voff; int reclen = vlen;
            if (wfb)
                dj_record_in(wfb, wlen, arrkey, idbuf, &rec, &reclen);
            int rc = isLeague
                ? DataJson_LoadCompetitionLeague(rec, reclen, slot, pool, DJ_NAMEPOOL)
                : DataJson_LoadCompetitionCup(rec, reclen, slot, 0, pool, DJ_NAMEPOOL);
            if (rc != 0) continue;
            (*poolIdx)++;
            struct dj_graph_reg *e = &reg[(*regN)++];
            strncpy(e->id, idbuf, sizeof e->id - 1); e->id[sizeof e->id - 1] = '\0';
            e->comp = (Competition *)slot;
            /* A league with a non-empty playoff_links array reaches a Playoffs +
               league-block (Leagues struct) whose sub-league pointers reference
               the COMPILED globals — those blocks aren't rebuilt from JSON yet, so
               swapping such a country leaves the division-loop (JSON copy) and the
               qualification walk (compiled Leagues block) mismatched and the
               season-transition scratch checksum panics (0x4444). Flag them so the
               install leaves those countries compiled. */
            e->hasPlayoffLinks = 0;
            if (isLeague) {
                const char *pl; int pll;
                if (mjson_find(rec, reclen, "$.playoff_links", &pl, &pll) == MJSON_TOK_ARRAY) {
                    int p2 = 0, vo, vl, t2;
                    if (fast_array_next(pl, pll, p2, &vo, &vl, &t2) != 0) e->hasPlayoffLinks = 1;
                }
            }
            (*n)++;
        }
    }
    free(wfb);
    free(fb);
}

static Competition *dj_graph_resolve(struct dj_graph_reg *reg, int regN, const char *id)
{
    for (int i = 0; i < regN; i++)
        if (strcmp(reg[i].id, id) == 0) return reg[i].comp;
    return (Competition *)0;
}

/* Resolve every string id in the JSON array (arrj/arrl) via the registry,
   appending the Competition* pointers into tbl[] (bounded by *k < limit).
   Returns 1 if all resolved, 0 if any id was missing (caller aborts the swap
   and leaves that country compiled). */
static int dj_graph_append_ids(const char *arrj, int arrl, struct dj_graph_reg *reg, int regN,
                               Competition **tbl, int *k, int limit)
{
    int pos = 0, voff, vlen, vt; char idb[40];
    while ((pos = fast_array_next(arrj, arrl, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_STRING) continue;
        if (mjson_get_string(arrj + voff, vlen, "$", idb, (int)sizeof idb) <= 0) continue;
        Competition *c = dj_graph_resolve(reg, regN, idb);
        if (!c) return 0;
        if (*k < limit) tbl[(*k)++] = c;
    }
    return 1;
}

/* A country is "complex" if any of its leagues carries playoff_links. Such a
   league's season-transition qualification code (processDiyLeagueQualificationEntry)
   is hardcoded by NAME to the compiled englishLeague / englishLeagues1 / etc. and
   gates on `competition == &englishLeague` (by address) — so the country CANNOT be
   served from a separate JSON copy (the address check fails, the promotion/
   relegation scratch weights never clear, panic 0x4444). Instead these countries
   are made data-driven by overwriting the compiled globals IN PLACE (see
   dj_inplace_load), keeping competitionsTable pointing at the compiled table.
   Returns 1 if complex. */
static int dj_graph_country_is_complex(const char *lj, int ljl,
                                       struct dj_graph_reg *reg, int regN)
{
    int pos = 0, voff, vlen, vt; char idb[40];
    while ((pos = fast_array_next(lj, ljl, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_STRING) continue;
        if (mjson_get_string(lj + voff, vlen, "$", idb, (int)sizeof idb) <= 0) continue;
        for (int i = 0; i < regN; i++)
            if (reg[i].hasPlayoffLinks && strcmp(reg[i].id, idb) == 0) return 1;
    }
    return 0;
}

/* Overwrite compiled competition globals IN PLACE with JSON data, pairing each
   JSON id (in array order) with compGlobals[i] (the compiled globals walked out
   of competitionsTable in the same order — inc3 proved the orders match). The
   competitionsTable pointer is left untouched, so every by-name / by-address
   reference (englishLeague, englishLeagues1->league1, the `==&englishLeague`
   gate) keeps seeing one object that now holds JSON data. A league's compiled
   playoffLinkPtrs (→ Playoffs + Leagues block) are PRESERVED across the
   overwrite, and the overwrite only happens when the parsed compId matches
   the compiled one (defensive — never corrupt a mispaired global). Returns the
   number overwritten. */
static int dj_inplace_load(const char *ids, int idsLen, int isLeague,
                           Competition **compGlobals, int nGlobals, int *poolIdx)
{
    if (idsLen <= 0) return 0;
    const char *file = isLeague ? "data_json/competitions/leagues.json"
                                : "data_json/competitions/cups.json";
    const char *key  = isLeague ? "leagues" : "competitions";
    int pos = 0, voff, vlen, vt, gi = 0, loaded = 0; char idb[40];
    while (gi < nGlobals && (pos = fast_array_next(ids, idsLen, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_STRING) continue;
        if (mjson_get_string(ids + voff, vlen, "$", idb, (int)sizeof idb) <= 0) continue;
        char *recfb; const char *o; int ol;
        if (!dj_find_comp_record(file, key, idb, &recfb, &o, &ol)) { gi++; continue; }
        if (*poolIdx >= DJ_MAX_RECS) { free(recfb); break; }
        char *pool = s_djNamePool[*poolIdx];
        Competition *G = compGlobals[gi];
        if (isLeague) {
            CompetitionLeague tmp;
            if (DataJson_LoadCompetitionLeague(o, ol, &tmp, pool, DJ_NAMEPOOL) == 0
                && tmp.header.comp.compId == ((CompetitionLeague *)G)->header.comp.compId) {
                CompetitionLeague *L = (CompetitionLeague *)G;
                char *p0 = L->playoffLinkPtrs[0], *p1 = L->playoffLinkPtrs[1];
                *L = tmp;                               /* names point into the persistent pool */
                L->playoffLinkPtrs[0] = p0; L->playoffLinkPtrs[1] = p1;   /* keep compiled plumbing */
                (*poolIdx)++; loaded++;
            }
        } else {
            CompetitionCup tmp;
            if (DataJson_LoadCompetitionCup(o, ol, &tmp, ((Competition *)G)->isPlayoff, pool, DJ_NAMEPOOL) == 0
                && tmp.header.comp.compId == ((CompetitionCup *)G)->header.comp.compId) {
                *(CompetitionCup *)G = tmp;
                (*poolIdx)++; loaded++;
            }
        }
        free(recfb);
        gi++;
    }
    return loaded;
}

void DataJson_InstallCompetitionGraph(void)
{
    static int installed = 0;
    if (installed) return;
    /* SWOS_DATA_JSON is default-ON; set SWOS_DATA_JSON=0 to fall back to the
       compiled competition tables. */
    { const char *v = getenv("SWOS_DATA_JSON"); if (v && v[0] == '0' && v[1] == '\0') return; }
    /* Isolation knob: SWOS_DATA_JSON_NO_COMP keeps the teams JSON loader on but
       leaves the compiled competitionsTable in place (so the two SWOS_DATA_JSON
       effects — JSON teams vs JSON competition graph — can be tested apart). */
    if (getenv("SWOS_DATA_JSON_NO_COMP")) return;
    installed = 1;   /* single attempt; any country left unswapped stays compiled */

    static struct dj_graph_reg reg[DJ_MAX_RECS];
    int regN = 0, nCups = 0, nLeagues = 0, poolIdx = 0;
    dj_graph_parse_file("data_json/competitions/cups.json",    "competitions", 0,
                        s_djCups,    sizeof s_djCups[0],    DJ_MAX_CUPS,    &nCups,
                        reg, &regN, DJ_MAX_RECS, &poolIdx);
    dj_graph_parse_file("data_json/competitions/leagues.json", "leagues",      1,
                        s_djLeagues, sizeof s_djLeagues[0], DJ_MAX_LEAGUES, &nLeagues,
                        reg, &regN, DJ_MAX_RECS, &poolIdx);

    int len = 0;
    char *fb = dj_slurp("data_json/competitions/country_competitions.json", &len);
    if (!fb) { fprintf(stderr, "[data-json] graph install: cannot open country_competitions.json\n"); return; }
    extern void *_competitionsTable[];
    const char *arr; int al; int tableN = 0, swapped = 0, inPlace = 0;
    if (mjson_find(fb, len, "$.countries", &arr, &al) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt; double v;
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            const char *lj, *cj; int ljl, cjl;
            int hasL = mjson_find(arr + voff, vlen, "$.leagues", &lj, &ljl) == MJSON_TOK_ARRAY;
            int hasC = mjson_find(arr + voff, vlen, "$.cups",    &cj, &cjl) == MJSON_TOK_ARRAY;
            if (!hasL && !hasC) continue;            /* region/special — keep compiled */
            int number = mjson_get_number(arr + voff, vlen, "$.number", &v) ? (int)v : -1;
            if (number < 0 || number > 255) continue;

            /* Complex (playoff-link) countries: cannot be served from a separate
               JSON copy (the qualification code is hardcoded to &englishLeague by
               address/name). Make them data-driven by overwriting the compiled
               globals IN PLACE, leaving competitionsTable pointing at the compiled
               table. */
            if (hasL && dj_graph_country_is_complex(lj, ljl, reg, regN)) {
                Competition **ctab = (Competition **)_competitionsTable[number];
                if (!ctab) continue;
                Competition *cL[DJ_TABLE_SLOTS], *cC[DJ_TABLE_SLOTS]; int nLg = 0, nCp = 0, sep = 0;
                for (int i = 0; i < DJ_TABLE_SLOTS; i++) {
                    intptr_t pv = (intptr_t)ctab[i];
                    if (pv == -1) break;
                    if (pv == -2) { sep = 1; continue; }
                    if (pv > -0x1000 && pv < 0x1000) continue;
                    if (!sep) { if (nLg < DJ_TABLE_SLOTS) cL[nLg++] = ctab[i]; }
                    else      { if (nCp < DJ_TABLE_SLOTS) cC[nCp++] = ctab[i]; }
                }
                if (hasL) dj_inplace_load(lj, ljl, 1, cL, nLg, &poolIdx);
                if (hasC) dj_inplace_load(cj, cjl, 0, cC, nCp, &poolIdx);
                inPlace++;
                continue;                            /* keep competitionsTable[number] compiled */
            }

            /* Simple countries: build a fresh JSON table and swap the pointer. */
            if (tableN >= DJ_MAX_TABLES) break;
            Competition **tbl = s_djCountryTables[tableN];
            int k = 0;
            /* leagues (before the separator) */
            if (hasL && !dj_graph_append_ids(lj, ljl, reg, regN, tbl, &k, DJ_TABLE_SLOTS - 2))
                continue;                            /* unresolved id — leave compiled */
            tbl[k++] = (Competition *)(intptr_t)-2;  /* league/cup separator */
            /* cups (after the separator) */
            if (hasC && !dj_graph_append_ids(cj, cjl, reg, regN, tbl, &k, DJ_TABLE_SLOTS - 1))
                continue;                            /* unresolved id — leave compiled */
            tbl[k++] = (Competition *)(intptr_t)-1;  /* terminator */
            _competitionsTable[number] = tbl;         /* install over the compiled pointer */
            tableN++; swapped++;
        }
    }
    free(fb);

    /* Standalone competition globals — the tournaments (European cups, World Cup,
       continental cups, WC/EC qualification) and promotion playoffs that the
       region QualifyingGroup tables (europe/world/customs) and league
       playoff-links reference. The region tables themselves stay COMPILED
       (structural containers, not Competition records); their underlying comps
       become JSON-driven here, overwritten IN PLACE (compId-matched) exactly
       like the 5 complex countries' leagues — preserving the address identity the
       by-name/by-address game plumbing depends on. id == global symbol name. */
    int nTour = 0, nPlay = 0, nDyn = 0;
    {
        extern CompetitionTournament _euroCup, _worldCup, _worldCup2,
            _northAmericaWorldCupQualification, _southAmericaWorldCupQualification,
            _africaWorldCupQualification, _asiaWorldCupQualification,
            _europeanChampionships, _europeanChampionships2,
            _copaAmerica, _africanNationsCup, _northAmericanCup, _asianCup;
        extern Playoffs _div1Playoffs, _div2Playoffs, _div3Playoffs,
            _dutchPlayoffs, _scottishPlayoffs, _spanishPlayoffs, _uruguayanPlayoffs;
        enum { GK_TOUR, GK_PLAYOFF };
        static const struct { const char *id; void *g; int kind; } sg[] = {
            { "_euroCup",                          &_euroCup,                          GK_TOUR },
            { "_worldCup",                         &_worldCup,                         GK_TOUR },
            { "_worldCup2",                        &_worldCup2,                        GK_TOUR },
            { "_northAmericaWorldCupQualification", &_northAmericaWorldCupQualification, GK_TOUR },
            { "_southAmericaWorldCupQualification", &_southAmericaWorldCupQualification, GK_TOUR },
            { "_africaWorldCupQualification",      &_africaWorldCupQualification,      GK_TOUR },
            { "_asiaWorldCupQualification",        &_asiaWorldCupQualification,        GK_TOUR },
            { "_europeanChampionships",            &_europeanChampionships,            GK_TOUR },
            { "_europeanChampionships2",           &_europeanChampionships2,           GK_TOUR },
            { "_copaAmerica",                      &_copaAmerica,                      GK_TOUR },
            { "_africanNationsCup",                &_africanNationsCup,                GK_TOUR },
            { "_northAmericanCup",                 &_northAmericanCup,                 GK_TOUR },
            { "_asianCup",                         &_asianCup,                         GK_TOUR },
            { "_div1Playoffs",      &_div1Playoffs,      GK_PLAYOFF },
            { "_div2Playoffs",      &_div2Playoffs,      GK_PLAYOFF },
            { "_div3Playoffs",      &_div3Playoffs,      GK_PLAYOFF },
            { "_dutchPlayoffs",     &_dutchPlayoffs,     GK_PLAYOFF },
            { "_scottishPlayoffs",  &_scottishPlayoffs,  GK_PLAYOFF },
            { "_spanishPlayoffs",   &_spanishPlayoffs,   GK_PLAYOFF },
            { "_uruguayanPlayoffs", &_uruguayanPlayoffs, GK_PLAYOFF },
        };
        for (unsigned gi = 0; gi < sizeof sg / sizeof sg[0]; gi++) {
            if (poolIdx >= DJ_MAX_RECS) break;
            const char *file = sg[gi].kind == GK_PLAYOFF
                ? "data_json/competitions/playoffs.json"
                : "data_json/competitions/tournaments.json";
            const char *arrkey = sg[gi].kind == GK_PLAYOFF ? "playoffs" : "competitions";
            char *recfb; const char *ro; int rol;
            if (!dj_find_comp_record(file, arrkey, sg[gi].id, &recfb, &ro, &rol)) continue;
            char *pool = s_djNamePool[poolIdx];
            if (sg[gi].kind == GK_PLAYOFF) {
                Playoffs tmp;
                if (DataJson_LoadPlayoffs(ro, rol, &tmp, pool, DJ_NAMEPOOL) == 0
                    && tmp.comp.compId == ((Playoffs *)sg[gi].g)->comp.compId) {
                    *(Playoffs *)sg[gi].g = tmp;   /* names point into the persistent pool */
                    poolIdx++; nPlay++;
                }
            } else {
                CompetitionTournament tmp;
                if (DataJson_LoadCompetitionTournament(ro, rol, &tmp, pool, DJ_NAMEPOOL) == 0
                    && tmp.header.comp.compId == ((CompetitionTournament *)sg[gi].g)->header.comp.compId) {
                    *(CompetitionTournament *)sg[gi].g = tmp;
                    poolIdx++; nTour++;
                }
            }
            free(recfb);
        }

        /* Dynamic competitions (roadmap step 6): a tournament record in
           tournaments.json whose id is NOT in sg[] has no compiled global. Heap-
           allocate its record + a working *Copy, load it from JSON, append the
           {template, copy, sizeof} triple to _careerContests (so InitCareer builds
           the copy and the save reconstructs it), and register it by compId.
           This is what makes a brand-new competition addable as pure data. */
        extern int  Comp_AppendCareerContest(Competition *, Competition *, uint16_t);
        extern void Comp_Register(Competition *);
        {
            int dlen = 0;
            char *dfb = dj_slurp("data_json/competitions/tournaments.json", &dlen);
            if (dfb) {
                const char *darr; int dal;
                if (mjson_find(dfb, dlen, "$.competitions", &darr, &dal) == MJSON_TOK_ARRAY) {
                    int dpos = 0, dvoff, dvlen, dvt;
                    while ((dpos = fast_array_next(darr, dal, dpos, &dvoff, &dvlen, &dvt)) != 0) {
                        if (dvt != MJSON_TOK_OBJECT) continue;
                        char idb[40];
                        if (mjson_get_string(darr + dvoff, dvlen, "$.id", idb, (int)sizeof idb) <= 0)
                            idb[0] = '\0';
                        if (poolIdx >= DJ_MAX_RECS) break;
                        CompetitionTournament *t = (CompetitionTournament *)calloc(1, sizeof *t);
                        CompetitionTournament *cc = (CompetitionTournament *)calloc(1, sizeof *cc);
                        if (!t || !cc) { free(t); free(cc); continue; }
                        char *dpool = s_djNamePool[poolIdx];
                        if (DataJson_LoadCompetitionTournament(darr + dvoff, dvlen, t, dpool, DJ_NAMEPOOL) != 0) {
                            free(t); free(cc); continue;   /* malformed record */
                        }
                        /* Dynamic iff no compiled tournament global shares this record's
                           compId. Detect by compId, not by id STRING — robust to
                           the id naming convention used in the JSON (the sg[] string ids
                           and the JSON ids don't share a prefix convention). */
                        int rcn = (uint8_t)t->header.comp.compId, known = 0;
                        for (unsigned gi = 0; gi < sizeof sg / sizeof sg[0]; gi++)
                            if (sg[gi].kind == GK_TOUR
                                && ((CompetitionTournament *)sg[gi].g)->header.comp.compId == rcn) {
                                known = 1; break;
                            }
                        if (known) { free(t); free(cc); continue; }   /* compiled — skip, reuse pool slice */
                        if (Comp_AppendCareerContest((Competition *)t, (Competition *)cc,
                                                     (uint16_t)sizeof *t) != 0) {
                            free(t); free(cc); continue;   /* contest table full */
                        }
                        poolIdx++;                          /* commit the name-pool slice */
                        Comp_Register((Competition *)t);
                        nDyn++;
                        fprintf(stderr, "[data-json] dynamic tournament '%s' compId=%d "
                                "-> heap record + careerContests\n",
                                idb, rcn);
                    }
                }
                free(dfb);
            }
        }
    }

    /* Remaining special competition records that no country table and no entry in
       the standalone tournament/playoff list reaches: the European club cups, the
       WC/EC qualifying-group cups, the Oceania cup + qualifier, the career special
       cups (invitation/tour/friendly), the English cup variants, and customsLeague.
       They are referenced by NAME (career code) or via the region QualifyingGroup
       tables, so — like the 5 complex countries' leagues and the standalone
       tournaments — they are data-driven by overwriting the compiled global IN
       PLACE (compId-matched), keeping the address identity intact. With this
       block every Competition record (162/162) is JSON-driven. */
    int nSpec = 0;
    {
        extern CompetitionCup _cupWinnersCup, _uefaCup, _oceaniaCup,
            _oceaniaWorldCupQualification, _invitationTournament, _tour, _friendly,
            _englishCup_1, _englishLeagueCup_1,
            _worldCupQualifyingGroup1, _worldCupQualifyingGroup2, _worldCupQualifyingGroup3,
            _worldCupQualifyingGroup4, _worldCupQualifyingGroup5, _worldCupQualifyingGroup6,
            _worldCupQualifyingGroup7, _worldCupQualifyingGroup8,
            _ecQualifyingGroup1, _ecQualifyingGroup2, _ecQualifyingGroup3, _ecQualifyingGroup4,
            _ecQualifyingGroup5, _ecQualifyingGroup6, _ecQualifyingGroup7, _ecQualifyingGroup8;
        extern CompetitionLeague _customsLeague;
        enum { SK_CUP, SK_LEAGUE };
        static const struct { const char *id; void *g; int kind; } sp[] = {
            { "_cupWinnersCup",                &_cupWinnersCup,                SK_CUP },
            { "_uefaCup",                      &_uefaCup,                      SK_CUP },
            { "_oceaniaCup",                   &_oceaniaCup,                   SK_CUP },
            { "_oceaniaWorldCupQualification", &_oceaniaWorldCupQualification, SK_CUP },
            { "_invitationTournament",         &_invitationTournament,         SK_CUP },
            { "_tour",                         &_tour,                         SK_CUP },
            { "_friendly",                     &_friendly,                     SK_CUP },
            { "_englishCup_1",                 &_englishCup_1,                 SK_CUP },
            { "_englishLeagueCup_1",           &_englishLeagueCup_1,           SK_CUP },
            { "_worldCupQualifyingGroup1",     &_worldCupQualifyingGroup1,     SK_CUP },
            { "_worldCupQualifyingGroup2",     &_worldCupQualifyingGroup2,     SK_CUP },
            { "_worldCupQualifyingGroup3",     &_worldCupQualifyingGroup3,     SK_CUP },
            { "_worldCupQualifyingGroup4",     &_worldCupQualifyingGroup4,     SK_CUP },
            { "_worldCupQualifyingGroup5",     &_worldCupQualifyingGroup5,     SK_CUP },
            { "_worldCupQualifyingGroup6",     &_worldCupQualifyingGroup6,     SK_CUP },
            { "_worldCupQualifyingGroup7",     &_worldCupQualifyingGroup7,     SK_CUP },
            { "_worldCupQualifyingGroup8",     &_worldCupQualifyingGroup8,     SK_CUP },
            { "_ecQualifyingGroup1",           &_ecQualifyingGroup1,           SK_CUP },
            { "_ecQualifyingGroup2",           &_ecQualifyingGroup2,           SK_CUP },
            { "_ecQualifyingGroup3",           &_ecQualifyingGroup3,           SK_CUP },
            { "_ecQualifyingGroup4",           &_ecQualifyingGroup4,           SK_CUP },
            { "_ecQualifyingGroup5",           &_ecQualifyingGroup5,           SK_CUP },
            { "_ecQualifyingGroup6",           &_ecQualifyingGroup6,           SK_CUP },
            { "_ecQualifyingGroup7",           &_ecQualifyingGroup7,           SK_CUP },
            { "_ecQualifyingGroup8",           &_ecQualifyingGroup8,           SK_CUP },
            { "_customsLeague",                &_customsLeague,                SK_LEAGUE },
        };
        for (unsigned si = 0; si < sizeof sp / sizeof sp[0]; si++) {
            if (poolIdx >= DJ_MAX_RECS) break;
            const char *file = sp[si].kind == SK_LEAGUE
                ? "data_json/competitions/leagues.json"
                : "data_json/competitions/cups.json";
            const char *arrkey = sp[si].kind == SK_LEAGUE ? "leagues" : "competitions";
            char *recfb; const char *ro; int rol;
            if (!dj_find_comp_record(file, arrkey, sp[si].id, &recfb, &ro, &rol)) continue;
            char *pool = s_djNamePool[poolIdx];
            if (sp[si].kind == SK_LEAGUE) {
                CompetitionLeague tmp;
                CompetitionLeague *L = (CompetitionLeague *)sp[si].g;
                if (DataJson_LoadCompetitionLeague(ro, rol, &tmp, pool, DJ_NAMEPOOL) == 0
                    && tmp.header.comp.compId == L->header.comp.compId) {
                    char *p0 = L->playoffLinkPtrs[0], *p1 = L->playoffLinkPtrs[1];
                    *L = tmp;                                  /* names point into the persistent pool */
                    L->playoffLinkPtrs[0] = p0; L->playoffLinkPtrs[1] = p1;
                    poolIdx++; nSpec++;
                }
            } else {
                CompetitionCup tmp;
                CompetitionCup *C = (CompetitionCup *)sp[si].g;
                if (DataJson_LoadCompetitionCup(ro, rol, &tmp, C->header.comp.isPlayoff, pool, DJ_NAMEPOOL) == 0
                    && tmp.header.comp.compId == C->header.comp.compId) {
                    *C = tmp;
                    poolIdx++; nSpec++;
                }
            }
            free(recfb);
        }
    }

    /* Region QualifyingGroup tables (europe/world/customs/continents + the WC/EC
       qualifying-group containers). These are NOT Competition records — they are
       structural containers: compCup[9] is a token stream of competition-global
       pointers plus in-band sentinels (252=0xFC league-menu marker, -2=league/cup
       separator, -1=terminator), and countries[44] is a 0xFF-terminated member
       list. qualifying.json carries the exact token stream; the loader resolves
       each string id to its global via R[] (the same globals data-driven above —
       so the pointers come out identical to the compiled table) and casts numeric
       tokens to literal sentinel pointers. Each table is overwritten in place ONLY
       after a byte-identical memcmp against the compiled global, so this is a pure
       data-relocation with zero behaviour/save-format change. */
    int nRegion = 0, regionMiss = 0;
    {
        extern CompetitionTournament _euroCup, _worldCup, _europeanChampionships,
            _northAmericaWorldCupQualification, _southAmericaWorldCupQualification,
            _africaWorldCupQualification, _asiaWorldCupQualification,
            _copaAmerica, _africanNationsCup, _northAmericanCup, _asianCup;
        extern CompetitionCup _cupWinnersCup, _uefaCup, _oceaniaCup,
            _oceaniaWorldCupQualification,
            _worldCupQualifyingGroup1, _worldCupQualifyingGroup2, _worldCupQualifyingGroup3,
            _worldCupQualifyingGroup4, _worldCupQualifyingGroup5, _worldCupQualifyingGroup6,
            _worldCupQualifyingGroup7, _worldCupQualifyingGroup8,
            _ecQualifyingGroup1, _ecQualifyingGroup2, _ecQualifyingGroup3, _ecQualifyingGroup4,
            _ecQualifyingGroup5, _ecQualifyingGroup6, _ecQualifyingGroup7, _ecQualifyingGroup8;
        extern CompetitionLeague _customsLeague;
        extern QualifyingGroup _worldCupQualifyingGroups, _ecQualifyingGroups, _customsTable,
            _worldTable, _europeTable, _africaTable, _southAmericaTable, _northAmericaTable,
            _asiaTable, _oceaniaTable;
        /* competition-id -> &global, for every comp a region table can reference */
        static const struct { const char *id; void *g; } R[] = {
            { "_worldCup", &_worldCup }, { "_euroCup", &_euroCup },
            { "_europeanChampionships", &_europeanChampionships },
            { "_cupWinnersCup", &_cupWinnersCup }, { "_uefaCup", &_uefaCup },
            { "_africaWorldCupQualification", &_africaWorldCupQualification },
            { "_africanNationsCup", &_africanNationsCup },
            { "_southAmericaWorldCupQualification", &_southAmericaWorldCupQualification },
            { "_copaAmerica", &_copaAmerica },
            { "_northAmericaWorldCupQualification", &_northAmericaWorldCupQualification },
            { "_northAmericanCup", &_northAmericanCup },
            { "_asiaWorldCupQualification", &_asiaWorldCupQualification },
            { "_asianCup", &_asianCup },
            { "_oceaniaWorldCupQualification", &_oceaniaWorldCupQualification },
            { "_oceaniaCup", &_oceaniaCup }, { "_customsLeague", &_customsLeague },
            { "_worldCupQualifyingGroup1", &_worldCupQualifyingGroup1 },
            { "_worldCupQualifyingGroup2", &_worldCupQualifyingGroup2 },
            { "_worldCupQualifyingGroup3", &_worldCupQualifyingGroup3 },
            { "_worldCupQualifyingGroup4", &_worldCupQualifyingGroup4 },
            { "_worldCupQualifyingGroup5", &_worldCupQualifyingGroup5 },
            { "_worldCupQualifyingGroup6", &_worldCupQualifyingGroup6 },
            { "_worldCupQualifyingGroup7", &_worldCupQualifyingGroup7 },
            { "_worldCupQualifyingGroup8", &_worldCupQualifyingGroup8 },
            { "_ecQualifyingGroup1", &_ecQualifyingGroup1 },
            { "_ecQualifyingGroup2", &_ecQualifyingGroup2 },
            { "_ecQualifyingGroup3", &_ecQualifyingGroup3 },
            { "_ecQualifyingGroup4", &_ecQualifyingGroup4 },
            { "_ecQualifyingGroup5", &_ecQualifyingGroup5 },
            { "_ecQualifyingGroup6", &_ecQualifyingGroup6 },
            { "_ecQualifyingGroup7", &_ecQualifyingGroup7 },
            { "_ecQualifyingGroup8", &_ecQualifyingGroup8 },
        };
        /* region-table id -> &global QualifyingGroup target */
        static const struct { const char *id; QualifyingGroup *g; } QT[] = {
            { "_worldCupQualifyingGroups", &_worldCupQualifyingGroups },
            { "_ecQualifyingGroups", &_ecQualifyingGroups },
            { "_customsTable", &_customsTable }, { "_worldTable", &_worldTable },
            { "_europeTable", &_europeTable }, { "_africaTable", &_africaTable },
            { "_southAmericaTable", &_southAmericaTable },
            { "_northAmericaTable", &_northAmericaTable },
            { "_asiaTable", &_asiaTable }, { "_oceaniaTable", &_oceaniaTable },
        };
        int qlen = 0;
        char *qfb = dj_slurp("data_json/competitions/qualifying.json", &qlen);
        const char *garr; int gal;
        if (qfb && mjson_find(qfb, qlen, "$.groups", &garr, &gal) == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt; char idb[40];
            while ((pos = fast_array_next(garr, gal, pos, &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) continue;
                if (mjson_get_string(garr + voff, vlen, "$.id", idb, (int)sizeof idb) <= 0) continue;
                QualifyingGroup *G = NULL;
                for (unsigned i = 0; i < sizeof QT / sizeof QT[0]; i++)
                    if (strcmp(QT[i].id, idb) == 0) { G = QT[i].g; break; }
                if (!G) continue;
                QualifyingGroup tmp; memset(&tmp, 0, sizeof tmp);
                int ok = 1;
                /* compCup token stream */
                const char *cc; int ccl, k = 0;
                if (mjson_find(garr + voff, vlen, "$.comp_cup", &cc, &ccl) == MJSON_TOK_ARRAY) {
                    int p2 = 0, o2, l2, t2; double dv;
                    while (k < 9 && (p2 = fast_array_next(cc, ccl, p2, &o2, &l2, &t2)) != 0) {
                        if (t2 == MJSON_TOK_STRING) {
                            char nb[40]; void *p = NULL;
                            if (mjson_get_string(cc + o2, l2, "$", nb, (int)sizeof nb) <= 0) { ok = 0; break; }
                            for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++)
                                if (strcmp(R[i].id, nb) == 0) { p = R[i].g; break; }
                            if (!p) { fprintf(stderr, "[data-json]   region '%s': unresolved comp id '%s'\n", idb, nb); ok = 0; break; }
                            tmp.compCup[k++] = (Competition *)p;
                        } else if (t2 == MJSON_TOK_NUMBER) {
                            mjson_get_number(cc + o2, l2, "$", &dv);
                            tmp.compCup[k++] = (Competition *)(intptr_t)(int)dv;
                        }
                    }
                }
                /* countries byte list (zero-padded to 44 by the memset) */
                const char *co; int col, n = 0;
                if (mjson_find(garr + voff, vlen, "$.countries", &co, &col) == MJSON_TOK_ARRAY) {
                    int p3 = 0, o3, l3, t3; double dv;
                    while (n < 44 && (p3 = fast_array_next(co, col, p3, &o3, &l3, &t3)) != 0) {
                        if (t3 != MJSON_TOK_NUMBER) continue;
                        mjson_get_number(co + o3, l3, "$", &dv);
                        tmp.countries[n++] = (uint8_t)(int)dv;
                    }
                }
                if (!ok) continue;
                if (memcmp(&tmp, G, sizeof tmp) != 0) {     /* corpus drift — keep compiled, flag it */
                    fprintf(stderr, "[data-json]   region '%s': JSON != compiled (kept compiled)\n", idb);
                    regionMiss++; continue;
                }
                *G = tmp; nRegion++;
            }
        }
        free(qfb);
    }

    fprintf(stderr, "[data-json] competition graph installed: %d simple countries swapped, "
           "%d complex countries loaded in place (cups=%d leagues=%d); "
           "standalone globals in place: %d tournaments, %d playoffs, %d special records, "
           "%d region tables (%d kept-compiled); %d dynamic competition(s)\n",
           swapped, inPlace, nCups, nLeagues, nTour, nPlay, nSpec, nRegion, regionMiss, nDyn);
    fflush(stderr);
}

/* ======================================================================
 * Country-graph validation (selftest increment 3)
 *
 * Builds an id -> {compId,type} registry by parsing EVERY record in the
 * four competition corpus files with the loaders above (a corpus-wide smoke
 * test of the loaders), then verifies that country_competitions.json's
 * per-country leagues[]/cups[] lists reproduce the compiled competitionsTable[]
 * country tables — same membership, same order, same compNumbers. This proves
 * the JSON corpus + loaders + country graph are complete and self-consistent,
 * which is the prerequisite for the runtime country-graph wire-in (loading the
 * competition tables from JSON instead of the compiled competition_data.c).
 *
 * Region/special tables (europe/world/customs/...) carry no leagues[]/cups[]
 * in the JSON (they are QualifyingGroup structs with continental cups + member
 * lists) and are skipped here — they are a separate, later pass.
 * ====================================================================== */

struct dj_reg_entry { char id[40]; int compId; int type; int isPlayoff; };

enum { DJ_KIND_CUP, DJ_KIND_PLAYOFF, DJ_KIND_TOUR, DJ_KIND_LEAGUE };

union dj_any_comp { CompetitionCup cup; CompetitionTournament tour; CompetitionLeague leag; };

/* Parse every record in one corpus file (array at "$.<arrkey>") with the loader
   selected by `kind`, recording id+compId+type into reg[]. The names are
   interned into a throwaway pool (overwritten per record — only the identity is
   kept). Returns the number added, or -1 on file error. */
static int dj_reg_add_file(const char *path, const char *arrkey, int kind,
                           struct dj_reg_entry *reg, int *regN, int regMax)
{
    int len = 0;
    char *fb = dj_slurp(path, &len);
    if (!fb)
        return -1;
    const char *arr; int al; char apath[64];
    snprintf(apath, sizeof apath, "$.%s", arrkey);
    int added = 0;
    static char namepool[512];
    if (mjson_find(fb, len, apath, &arr, &al) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt;
        static union dj_any_comp scratch;
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            char idbuf[40];
            if (mjson_get_string(arr + voff, vlen, "$.id", idbuf, (int)sizeof idbuf) <= 0) continue;
            int rc = -1;
            switch (kind) {
            case DJ_KIND_CUP:     rc = DataJson_LoadCompetitionCup(arr + voff, vlen, &scratch, 0, namepool, (int)sizeof namepool); break;
            case DJ_KIND_PLAYOFF: rc = DataJson_LoadCompetitionCup(arr + voff, vlen, &scratch, 1, namepool, (int)sizeof namepool); break;
            case DJ_KIND_TOUR:    rc = DataJson_LoadCompetitionTournament(arr + voff, vlen, &scratch, namepool, (int)sizeof namepool); break;
            case DJ_KIND_LEAGUE:  rc = DataJson_LoadCompetitionLeague(arr + voff, vlen, &scratch, namepool, (int)sizeof namepool); break;
            }
            if (rc != 0) continue;   /* parse failure — leave out of registry (surfaces as a missing id) */
            if (*regN < regMax) {
                struct dj_reg_entry *e = &reg[(*regN)++];
                strncpy(e->id, idbuf, sizeof e->id - 1); e->id[sizeof e->id - 1] = '\0';
                Competition *c = (Competition *)&scratch;   /* comp base is at offset 0 of every record */
                e->compId = c->compId; e->type = c->type; e->isPlayoff = c->isPlayoff;
                added++;
            }
        }
    }
    free(fb);
    return added;
}

static int dj_reg_find(struct dj_reg_entry *reg, int regN, const char *id)
{
    for (int i = 0; i < regN; i++)
        if (strcmp(reg[i].id, id) == 0) return i;
    return -1;
}

/* Compare a JSON id array (arrj/arrl) against the compiled compId sequence
   comp[ncomp]: every id must resolve in the registry, in the same order, with a
   matching compId, and the counts must match. Returns 1 on full match. */
static int dj_cmp_idlist(const char *arrj, int arrl, struct dj_reg_entry *reg, int regN,
                         const int *comp, int ncomp, const char *what, int number)
{
    int pos = 0, voff, vlen, vt, k = 0, ok = 1; char idb[40];
    while ((pos = fast_array_next(arrj, arrl, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_STRING) continue;
        if (mjson_get_string(arrj + voff, vlen, "$", idb, (int)sizeof idb) <= 0) continue;
        int ri = dj_reg_find(reg, regN, idb);
        if (ri < 0) {
            printf("[data-json]   graph FAIL[%d]: %s id '%s' not in registry\n", number, what, idb); ok = 0; k++; continue;
        }
        if (k >= ncomp) {
            printf("[data-json]   graph FAIL[%d]: %s extra id '%s' (compiled has %d)\n", number, what, idb, ncomp); ok = 0; k++; continue;
        }
        if (reg[ri].compId != comp[k]) {
            printf("[data-json]   graph FAIL[%d]: %s '%s' compNum %d != compiled %d\n", number, what, idb, reg[ri].compId, comp[k]); ok = 0;
        }
        k++;
    }
    if (k != ncomp) {
        printf("[data-json]   graph FAIL[%d]: %s count %d != compiled %d\n", number, what, k, ncomp); ok = 0;
    }
    return ok;
}

static int dj_selftest_graph(void)
{
    struct dj_reg_entry reg[256]; int regN = 0;
    int nc = dj_reg_add_file("data_json/competitions/cups.json",        "competitions", DJ_KIND_CUP,     reg, &regN, 256);
    int np = dj_reg_add_file("data_json/competitions/playoffs.json",    "playoffs",     DJ_KIND_PLAYOFF, reg, &regN, 256);
    int nt = dj_reg_add_file("data_json/competitions/tournaments.json", "competitions", DJ_KIND_TOUR,    reg, &regN, 256);
    int nl = dj_reg_add_file("data_json/competitions/leagues.json",     "leagues",      DJ_KIND_LEAGUE,  reg, &regN, 256);
    printf("[data-json] graph: registry built — cups=%d playoffs=%d tournaments=%d leagues=%d total=%d\n",
           nc, np, nt, nl, regN);
    if (nc < 0 || np < 0 || nt < 0 || nl < 0) {
        printf("[data-json] graph: FAIL — a corpus file failed to load\n");
        return 0;
    }

    int len = 0;
    char *fb = dj_slurp("data_json/competitions/country_competitions.json", &len);
    if (!fb) { printf("[data-json] graph: cannot open country_competitions.json\n"); return 0; }
    extern void *_competitionsTable[];
    const char *arr; int al; int ok = 1, countries = 0, mismatches = 0;
    if (mjson_find(fb, len, "$.countries", &arr, &al) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt; double v;
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            const char *lj, *cj; int ljl, cjl;
            int hasL = mjson_find(arr + voff, vlen, "$.leagues", &lj, &ljl) == MJSON_TOK_ARRAY;
            int hasC = mjson_find(arr + voff, vlen, "$.cups",    &cj, &cjl) == MJSON_TOK_ARRAY;
            if (!hasL && !hasC) continue;   /* region/special table — skip */
            int number = mjson_get_number(arr + voff, vlen, "$.number", &v) ? (int)v : -1;
            if (number < 0 || number > 255) continue;
            Competition **tbl = (Competition **)_competitionsTable[number];
            if (!tbl) { printf("[data-json]   graph FAIL: number %d has no compiled table\n", number); ok = 0; mismatches++; continue; }
            /* Walk the compiled Competition*[] table: entries before the (-2)
               separator are leagues, after it are cups, (-1) terminates. */
            int compL[16], compC[16], nL = 0, nC = 0, sawSep = 0;
            for (int i = 0; i < 64; i++) {
                intptr_t pv = (intptr_t)tbl[i];
                if (pv == -1) break;
                if (pv == -2) { sawSep = 1; continue; }
                if (pv > -0x1000 && pv < 0x1000) continue;   /* region-only small sentinel; not expected here */
                int cnum = tbl[i]->compId;
                if (!sawSep) { if (nL < 16) compL[nL++] = cnum; }
                else         { if (nC < 16) compC[nC++] = cnum; }
            }
            int cok = 1;
            cok &= dj_cmp_idlist(hasL ? lj : "", hasL ? ljl : 0, reg, regN, compL, nL, "league", number);
            cok &= dj_cmp_idlist(hasC ? cj : "", hasC ? cjl : 0, reg, regN, compC, nC, "cup", number);
            if (!cok) { ok = 0; mismatches++; }
            countries++;
        }
    }
    free(fb);
    printf("[data-json] graph: country tables %s — %d countries checked, %d mismatched\n",
           ok ? "PASS" : "FAIL", countries, mismatches);
    return ok;
}

/* Self-test (SWOS_DATA_JSON_SELFTEST=1): load teams/SCOTLAND.json and check the
 * parse against known values. Proves the data-JSON loader builds the structs
 * correctly. Semantic checks (not a raw memcmp) so endian-sensitive fields like
 * globalTeamNumber are compared as logical values, not disk byte order. */
int DataJson_SelfTest(void)
{
    /* The selftest runs before main()'s Translations_Load, so load the English
     * table here — competition name slots are symbols resolved through tr(). */
    Translations_Load("en");

    /* CWD is anchored to the binary dir (bin/) by chdir_to_binary, like data/. */
    FILE *f = fopen("data_json/teams/SCOTLAND.json", "rb");
    if (!f) { printf("[data-json] selftest: cannot open data_json/teams/SCOTLAND.json\n"); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return 2; }
    buf[n] = '\0';
    fclose(f);

    static TeamFileEx t[64];
    int cnt = DataJson_LoadTeamArray(buf, (int)n, t, 64);
    free(buf);

    int ok = 1;
#define CHK(c) do { if (!(c)) { printf("[data-json]   FAIL: %s\n", #c); ok = 0; } } while (0)
    CHK(cnt == 40);
    CHK(t[0].team.countryNumber == 33);
    CHK(t[0].team.teamOrdinal == 0);
    CHK(memcmp(t[0].team.teamName, "ABERDEEN", 8) == 0);
    CHK(t[33].team.teamOrdinal == 33);
    CHK(memcmp(t[33].team.teamName, "RANGERS", 7) == 0);
    CHK(t[33].team.globalTeamNumber == 1393);
    CHK(memcmp(t[33].players[11].playerName, "PAUL GASCOIGNE", 14) == 0);
    CHK((uint8_t)t[33].players[11].positionAndFace == 0xC8);  /* MF + blonde */
    CHK((uint8_t)t[33].players[11].speedFinishing == 0x5F);   /* Sp5 F15 */
#undef CHK
    printf("[data-json] selftest teams: loaded=%d  %s\n", cnt, ok ? "PASS" : "FAIL");
    printf("[data-json]   t[0]=%.17s  t[33]=%.17s gtn=%d  p11=%.22s\n",
           t[0].team.teamName, t[33].team.teamName,
           t[33].team.globalTeamNumber, t[33].players[11].playerName);

    /* ---- competition record loader: load cups.json's "cupWinnersCup" and
       verify the parse against the compiled global of the same name. ---- */
    int cok = 1;
    extern CompetitionCup _cupWinnersCup;   /* compiled record */
    char *cj = NULL; { FILE *cf = fopen("data_json/competitions/cups.json", "rb");
        if (cf) { fseek(cf,0,SEEK_END); long cn2=ftell(cf); fseek(cf,0,SEEK_SET);
            cj=(char*)malloc((size_t)cn2+1); if (cj && fread(cj,1,(size_t)cn2,cf)==(size_t)cn2) cj[cn2]='\0'; else { free(cj); cj=NULL; }
            fclose(cf); } }
    if (!cj) { printf("[data-json] selftest comp: cannot open cups.json\n"); return ok ? 0 : 1; }
    int cjlen = (int)strlen(cj);
    const char *arr; int al;
    static CompetitionCup loaded;
    static char strpool[256];
    int found = 0;
    if (mjson_find(cj, cjlen, "$.competitions", &arr, &al) == MJSON_TOK_ARRAY) {
        int pos=0,voff,vlen,vt; char idbuf[64];
        while ((pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            if (mjson_get_string(arr+voff, vlen, "$.id", idbuf, (int)sizeof idbuf) > 0
                && strcmp(idbuf, "cupWinnersCup") == 0) {
                DataJson_LoadCompetitionCup(arr+voff, vlen, &loaded, 0, strpool, (int)sizeof strpool);
                found = 1; break;
            }
        }
    }
    free(cj);
#define CCHK(c) do { if (!(c)) { printf("[data-json]   comp FAIL: %s\n", #c); cok = 0; } } while (0)
    CCHK(found);
    CCHK(loaded.header.comp.compId == _cupWinnersCup.header.comp.compId);
    CCHK(loaded.header.comp.type == _cupWinnersCup.header.comp.type);
    CCHK(loaded.header.comp.seedReorderFlag == _cupWinnersCup.header.comp.seedReorderFlag);
    CCHK(loaded.header.teams == _cupWinnersCup.header.teams);
    CCHK(loaded.roundRuleFlags[0] == _cupWinnersCup.roundRuleFlags[0]);
    CCHK(loaded.name[0] && _cupWinnersCup.name[0] && strcmp(loaded.name[0], _cupWinnersCup.name[0]) == 0);
    CCHK(loaded.name[1] && _cupWinnersCup.name[1] && strcmp(loaded.name[1], _cupWinnersCup.name[1]) == 0);
    CCHK(loaded.teamSeedList[0].countryNumber == _cupWinnersCup.teamSeedList[0].countryNumber);
    CCHK(loaded.teamSeedList[0].teamOrdinal   == _cupWinnersCup.teamSeedList[0].teamOrdinal);
    CCHK(loaded.teamSeedList[4].countryNumber == _cupWinnersCup.teamSeedList[4].countryNumber);
#undef CCHK
    printf("[data-json] selftest comp: cupWinnersCup %s\n", cok ? "PASS" : "FAIL");
    printf("[data-json]   teams=%d name0=%.30s seed[0]=%u/%u seed[4]=%u/%u\n",
           loaded.header.teams, loaded.name[0] ? loaded.name[0] : "(null)",
           loaded.teamSeedList[0].countryNumber, loaded.teamSeedList[0].teamOrdinal,
           loaded.teamSeedList[4].countryNumber, loaded.teamSeedList[4].teamOrdinal);

    /* ---- tournament loader: load tournaments.json's "euroCup" and verify ---- */
    int tok = 1;
    extern CompetitionTournament _euroCup;   /* compiled record */
    { char *tj = NULL; const char *to; int tol;
      if (dj_find_comp_record("data_json/competitions/tournaments.json", "competitions",
                              "euroCup", &tj, &to, &tol)) {
        static CompetitionTournament tl; static char tsp[256];
        DataJson_LoadCompetitionTournament(to, tol, &tl, tsp, (int)sizeof tsp);
        free(tj);
#define TCHK(c) do { if (!(c)) { printf("[data-json]   tour FAIL: %s\n", #c); tok = 0; } } while (0)
        TCHK(tl.header.comp.compId == _euroCup.header.comp.compId);
        TCHK(tl.header.comp.type == _euroCup.header.comp.type);     /* 2 */
        TCHK(tl.header.seedingType == _euroCup.header.seedingType);
        TCHK(tl.header.playEachTeam == _euroCup.header.playEachTeam);
        TCHK(tl.header.roundStringBase == _euroCup.header.roundStringBase);
        TCHK(tl.header.rounds[0].teamCount == _euroCup.header.rounds[0].teamCount);
        TCHK(tl.header.rounds[0].groupCount == _euroCup.header.rounds[0].groupCount);
        TCHK(tl.header.rounds[1].teamCount == _euroCup.header.rounds[1].teamCount);
        TCHK(tl.name[0] && _euroCup.name[0] && strcmp(tl.name[0], _euroCup.name[0]) == 0);
        TCHK(tl.teamSeedList[0].countryNumber == _euroCup.teamSeedList[0].countryNumber);
        TCHK(tl.teamSeedList[0].teamOrdinal   == _euroCup.teamSeedList[0].teamOrdinal);
#undef TCHK
        printf("[data-json] selftest comp: euroCup %s — type=%d seed=%d r0=%d/%d name0=%.30s seed[0]=%u/%u\n",
               tok ? "PASS" : "FAIL", tl.header.comp.type, tl.header.seedingType,
               tl.header.rounds[0].teamCount, tl.header.rounds[0].groupCount,
               tl.name[0] ? tl.name[0] : "(null)",
               tl.teamSeedList[0].countryNumber, tl.teamSeedList[0].teamOrdinal);
      } else { printf("[data-json] selftest comp: euroCup NOT FOUND\n"); tok = 0; }
    }

    /* ---- league loader: load leagues.json's "englishLeague" and verify ---- */
    int lok = 1;
    extern CompetitionLeague _englishLeague;   /* compiled record */
    { char *lj = NULL; const char *lo; int lol;
      if (dj_find_comp_record("data_json/competitions/leagues.json", "leagues",
                              "englishLeague", &lj, &lo, &lol)) {
        static CompetitionLeague ll; static char lsp[256];
        DataJson_LoadCompetitionLeague(lo, lol, &ll, lsp, (int)sizeof lsp);
        free(lj);
#define LCHK(c) do { if (!(c)) { printf("[data-json]   leag FAIL: %s\n", #c); lok = 0; } } while (0)
        LCHK(ll.header.comp.compId == _englishLeague.header.comp.compId);
        LCHK(ll.header.comp.type == _englishLeague.header.comp.type);   /* 0 */
        LCHK(ll.header.divisionsCount == _englishLeague.header.divisionsCount);
        LCHK(ll.header.pointsForWin == _englishLeague.header.pointsForWin);
        LCHK(ll.header.subsAndMaxSubs == _englishLeague.header.subsAndMaxSubs);
        LCHK(ll.divisions[0].teams == _englishLeague.divisions[0].teams);
        LCHK(ll.divisions[0].relegated == _englishLeague.divisions[0].relegated);
        LCHK(ll.divisions[1].teams == _englishLeague.divisions[1].teams);
        LCHK(ll.name[0] && _englishLeague.name[0] && strcmp(ll.name[0], _englishLeague.name[0]) == 0);
        LCHK(ll.name[2] && _englishLeague.name[2] && strcmp(ll.name[2], _englishLeague.name[2]) == 0);
        LCHK(ll.promotionLinkData[3] == _englishLeague.promotionLinkData[3]);
        LCHK(ll.promotionLinkData[8] == _englishLeague.promotionLinkData[8]); /* 0xFF sentinel */
#undef LCHK
        printf("[data-json] selftest comp: englishLeague %s — type=%d divs=%d d0.teams=%d name0=%.30s plink[3]=%u plink[8]=%u\n",
               lok ? "PASS" : "FAIL", ll.header.comp.type, ll.header.divisionsCount,
               ll.divisions[0].teams, ll.name[0] ? ll.name[0] : "(null)",
               ll.promotionLinkData[3], ll.promotionLinkData[8]);
      } else { printf("[data-json] selftest comp: englishLeague NOT FOUND\n"); lok = 0; }
    }

    /* ---- whole-corpus registry + country-graph topology validation ---- */
    int gok = dj_selftest_graph();

    return (ok && cok && tok && lok && gok) ? 0 : 1;
}
#endif /* SWOS_SDL_BUILD */

/* The named-blob ("blobs":{}) sub-object and the previous base64
 * payload-per-region scheme have been retired. Every region that used
 * to live in s_career_blobs is now either a typed scalar in
 * s_career_scalars (FLD_BYTES or otherwise) or a structured field at
 * the top level (tmd_teams, selected_teams, career_foreign_market_players,
 * career_team_reserves, current_team_reserves). The two transient menu-
 * scratch regions (editTeamsBuffer, editTeamsSaveVarsArea) were dropped
 * entirely because no code path reads them outside the menu they belong
 * to. Cross-rebuild loads now rely on RebuildPointersAfterLoad() for
 * any pointer field re-derivation (see SaveJson_LoadCareer). */

/* ---------- *Copy cup-record nameSource pointers ----------
 *
 * Each entry in the static careerContests table maps a template
 * competition record to its runtime *Copy inside competitionFileBuffer.
 * Each *Copy carries a `nameSource` pointer at offset
 * `record[5] + 5` (the byte at record[5] is the offset of the
 * nameSource field within the record). Those pointers are saved with
 * the *Copy buffer and need symbolic encoding to survive rebuilds.
 *
 * Layout per careerContests entry (10 bytes):
 *   +0 src  ptr  (template static record — not saved)
 *   +4 dest ptr  (the *Copy in competitionFileBuffer — not saved)
 *   +8 size word (byte count — not saved)
 *
 * The 0xFFFFFFFF src value terminates the table.
 */

extern CompetitionCareer _careerContests[];

/* ---------- *Copy struct text[2] pointers ----------
 *
 * Each *Copy competition struct (euroCupCopy, cupWinnersCupCopy, …
 * 34 of them in careerContests) has a `text[2]` pointer pair living
 * at offset 5 + src[5] within the struct. text[0] / text[1] point
 * into .rodata (`aEuropeanChampi_1`, `aEuropeanCup`, etc.) and are
 * baked in at link time when the static template (e.g. `euroCup`) is
 * initialised. The runtime *Copy is created by memcpy-ing the
 * template; nothing ever modifies text[0]/text[1] after that.
 *
 * So there's nothing to save: the bytes are pure template state.
 * On load we just copy text[0..1] from each src template into its
 * dest *Copy, overwriting whatever stale pointer bytes the raw-byte
 * blob load left behind. The struct's own byte-5 field tells us the
 * exact offset — no scan, no heuristic, no JSON encoding needed.
 *
 * Replaces the previous cup_record_name_sources (single-pointer per
 * *Copy, encoded as JSON) and comp_copy_pointer_fixups (heuristic
 * byte-scan) — both unnecessary once we recognise the values are
 * template-static. */
static void restore_comp_copy_text_ptrs_from_template(void)
{
    /* careerContests is a CompetitionCareer[] (two pointers + uint16
     * length). The Amiga path used `uint8_t *tbl; tbl += 10` which is
     * the 32-bit packed stride; on 64-bit each entry is 18 bytes
     * (two 8-byte pointers + 2). Iterate as the typed array so stride
     * is correct on both targets, and compare the sentinel value as
     * a low-32 pattern (the static table stores
     * `(CompetitionCup *)0xFFFFFFFFu` which zero-extends to
     * 0x00000000FFFFFFFFu on 64-bit). */
    extern CompetitionCareer _careerContests[];
    for (CompetitionCareer *e = _careerContests;
         (uintptr_t)e->competition != 0xFFFFFFFFu; e++) {
        const uint8_t *src  = (const uint8_t *)e->competition;
        uint8_t       *dest = (uint8_t *)e->competitionCopy;
        int8_t nameOffset = (int8_t)src[5];
        if (nameOffset != 0) {
            /* text[0] + text[1] = 8 bytes from src template to dest
             * *Copy at the struct's own text-array offset. */
            memcpy(dest + 5 + nameOffset, src + 5 + nameOffset, 8);
        }
    }
}

/* ---------- comp_copy_region: reconstruct from templates ----------
 *
 * The 34 *Copy competition records (euroCupCopy..oceaniaWorldCupQualifi-
 * cationCopy) used to be saved as one raw byte blob (`comp_copy_region`).
 * That blob is NOT cross-target safe: each record embeds a `text[2]`
 * POINTER pair (4 bytes 32-bit / 8 bytes 64-bit), so everything after it
 * — including the embedded team-id list the season auto-advance reads —
 * sits at a different byte offset per target. A 64-bit save loaded on
 * 32-bit then reads the team-id list 8 bytes early → bogus ids (ordinal 0)
 * → AppendTeamsToSelectionFromTmdOrTeamFile spins forever. (Observed:
 * 32-bit hang on a 64-bit save; 64-bit same-target was fine.)
 *
 * The records are almost entirely STATIC: career_init.c:687-702 builds
 * each *Copy by a verbatim sizeof()-based memcpy of its static template
 * (careerContests[].competition → .competitionCopy). The ONLY per-career
 * dynamic state across all 34 records is two single bytes at offset 0xD
 * (firstRoundNo, repurposed as a team-count): worldCupQualifyingGroup8Copy
 * and oceaniaWorldCupQualificationCopy (career_season.c:1012/1078).
 *
 * So instead of typing the blob, we RECONSTRUCT: re-copy every template →
 * *Copy on load (host-correct sizes, immune to layout by construction),
 * then re-apply the two saved dynamic bytes, then fix the text pointers.
 * This drops the blob entirely and is layout-safe on every target. */
extern uint8_t _worldCupQualifyingGroup8Copy[];
extern uint8_t _oceaniaWorldCupQualificationCopy[];

static void reconstruct_comp_copy_region_from_templates(void)
{
    extern CompetitionCareer _careerContests[];
    for (CompetitionCareer *e = _careerContests;
         (uintptr_t)e->competition != 0xFFFFFFFFu; e++) {
        const uint8_t *src = (const uint8_t *)e->competition;
        uint8_t       *dst = (uint8_t *)e->competitionCopy;
        uint16_t       n   = e->templateSize;     /* sizeof(template), per-target */
        memcpy(dst, src, n);
    }
}

/* Emit the two dynamic *Copy bytes (offset 0xD). Byte 0xD is in the
 * CompetitionCup base (well before text[]), so it's layout-stable. */
static void emit_comp_copy_dynamic(mjson_print_fn_t fn, void *fnd)
{
    mjson_printf(fn, fnd, "%Q:{%Q:%u,%Q:%u}", "comp_copy_dynamic",
                 "wc_group8_teams",  (unsigned)_worldCupQualifyingGroup8Copy[0xD],
                 "oceania_qual_teams",(unsigned)_oceaniaWorldCupQualificationCopy[0xD]);
}

/* Save each *Copy competition record's per-career dynamic team-list region
 * (unk1[]). reconstruct_comp_copy_region_from_templates() resets the *Copy
 * records to their static TEMPLATES on load — which WIPES the career's actual
 * cup brackets. On a loaded career the background euro cups then auto-advance
 * from TEMPLATE teams, and a template team (e.g. FC PORTO, the euroCup
 * template's last entry) can become a spurious title holder that collides with
 * the player's real cup holder -> the 0x3443 TMD-pool dup
 * (project_eurocup_draw_dup_nondeterministic). The 2696 "almost entirely
 * static" assumption missed that qualification writes the qualified teams INTO
 * these records (euroCupTeamListPtr == euroCupCopy+offset). Locate the list
 * exactly as Career_PrepareEuropeanCupQualificationState does:
 *   off = 7 + base[7] + 2*(sizeof(char*) - 4)   // PAST the text[2] pointers
 *   len = templateSize - off
 * so NO pointer bytes are serialized, and the BE {country,ordinal} byte pairs
 * are endian-/target-safe (len is target-independent: both off and templateSize
 * grow by the same text[2] delta on 64-bit). One sub-array per careerContests
 * record, in table order; records with no team list (off before text[2], or
 * past the record) emit []. */
static void emit_comp_copy_teamlists(mjson_print_fn_t fn, void *fnd)
{
    extern CompetitionCareer _careerContests[];
    mjson_printf(fn, fnd, "%Q:[", "comp_copy_teamlists");
    int first = 1;
    for (CompetitionCareer *e = _careerContests;
         (uintptr_t)e->competition != 0xFFFFFFFFu; e++) {
        const uint8_t *base = (const uint8_t *)e->competitionCopy;
        int8_t nameOffset = (int8_t)base[5];
        uint16_t text_end = (nameOffset != 0)
            ? (uint16_t)(5 + nameOffset + 2u * sizeof(char *)) : 7u;
        uint16_t off = (uint16_t)(7u + base[7]
                       + 2u * (sizeof(char *) - sizeof(uint32_t)));
        if (!first) mjson_printf(fn, fnd, "%s", ",");
        first = 0;
        if (off < text_end || off >= e->templateSize) {
            mjson_printf(fn, fnd, "%s", "[]");   /* no team list / unsafe */
            continue;
        }
        uint16_t len = (uint16_t)(e->templateSize - off);
        mjson_printf(fn, fnd, "%s", "[");
        for (uint16_t i = 0; i < len; i++) {
            if (i) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)base[off + i]);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }
    mjson_printf(fn, fnd, "%s", "]");
}

/* Reconstruct the *Copy region: templates → *Copy, then re-apply the two
 * saved dynamic bytes. Pointers are fixed separately by
 * restore_comp_copy_text_ptrs_from_template (called by the load path). */
static void load_comp_copy_region(const char *buf, int buflen)
{
    reconstruct_comp_copy_region_from_templates();

    const char *o = NULL; int olen = 0;
    if (mjson_find(buf, buflen, "$.comp_copy_dynamic", &o, &olen)
            == MJSON_TOK_OBJECT) {
        long v;
        if (mjson_get_long(o, olen, "$.wc_group8_teams", &v) > 0)
            _worldCupQualifyingGroup8Copy[0xD] = (uint8_t)v;
        if (mjson_get_long(o, olen, "$.oceania_qual_teams", &v) > 0)
            _oceaniaWorldCupQualificationCopy[0xD] = (uint8_t)v;
    }

    /* Restore the per-career *Copy team-list regions OVER the just-reconstructed
     * templates (see emit_comp_copy_teamlists). Lockstep walk of careerContests
     * and the saved sub-arrays. Absent in pre-fix saves -> mjson_find misses ->
     * templates stand (old behavior, still subject to the euro-cup dup until the
     * career is re-saved on this build). off/len recomputed per record exactly as
     * on emit (base[5]/base[7] are static descriptor fields, so identical after
     * reconstruct); the guard keeps us past text[2] and inside the record. */
    {
        extern CompetitionCareer _careerContests[];
        const char *arr = NULL; int alen = 0;
        if (mjson_find(buf, buflen, "$.comp_copy_teamlists", &arr, &alen)
                == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt;
            CompetitionCareer *e = _careerContests;
            while ((uintptr_t)e->competition != 0xFFFFFFFFu &&
                   (pos = fast_array_next(arr, alen, pos, &voff, &vlen, &vt)) != 0) {
                if (vt == MJSON_TOK_ARRAY) {
                    uint8_t *base = (uint8_t *)e->competitionCopy;
                    int8_t nameOffset = (int8_t)base[5];
                    uint16_t text_end = (nameOffset != 0)
                        ? (uint16_t)(5 + nameOffset + 2u * sizeof(char *)) : 7u;
                    uint16_t off = (uint16_t)(7u + base[7]
                                   + 2u * (sizeof(char *) - sizeof(uint32_t)));
                    if (off >= text_end && off < e->templateSize) {
                        parse_byte_array_slice(arr + voff, vlen, base + off,
                                               (uint32_t)(e->templateSize - off));
                    }
                }
                e++;
            }
        }
    }
}

/* ---------- teamSelectionKeyCache host-native fixup ----------
 *
 * The cache (raw FLD_BYTES blob) has host-native multi-byte fields that
 * byte-swap cross-target. Two kinds, both fixed here by emitting their
 * VALUES as endian-neutral decimals (walking the entry list) and re-laying
 * them host-native on load:
 *   1. Each entry's +2 uint16 stride. Swapping breaks the entry walk —
 *      the runtime walker's stride<10||>256 bounds check rejects the
 *      swapped value and bails after one entry (one-fixture-on-Amiga bug).
 *   2. Each game-record's score fields: packedGoals(+6) / aggregate(+8) /
 *      penaltyScore(+10), host-native u16s. Swapping flips home/away
 *      scores (VFL BOCHUM 1-0 shown as 0-1 — live-gdb-confirmed). The
 *      12-byte game records start at entry+0x0A; numGames is entry[5].
 * Team-ids in the records (+0x0C/+0x0E) are BE byte-pairs (BE_WRITE/READ_U16)
 * → endian-stable, NOT fixed here.
 *
 * cache_strides[] holds one stride per entry; cache_scores[] holds 3
 * decimals (packed,agg,pen) per game record, flattened in walk order.
 * Both walks use the LIVE/just-restored strides + numGames; bounds-guarded
 * (stride [10,256], stay within TSKC_SIZE) so corrupt data can't overrun. */
extern uint16_t _teamSelectionKeyCacheCount;
#define TSKC_SIZE 1870u
#define TSKC_GAMEREC_OFF 0x0A      /* game records start here within an entry */

static void emit_cache_strides(mjson_print_fn_t fn, void *fnd)
{
    uint8_t *base = _teamSelectionKeyCache;
    int count = (int)(int16_t)_teamSelectionKeyCacheCount;

    /* Pass 1: strides (one per entry). */
    mjson_printf(fn, fnd, "%Q:[", "cache_strides");
    uint32_t off = 0;
    for (int i = 0; i < count; i++) {
        if (off + 4 > TSKC_SIZE) break;
        uint16_t stride = *(uint16_t *)(base + off + 2);
        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        mjson_printf(fn, fnd, "%u", (unsigned)stride);
        if (stride < 10 || stride > 256) break;
        off += stride;
    }
    mjson_printf(fn, fnd, "%s", "]");

    /* Pass 2: per-game-record score fields (packed,agg,pen) in walk order. */
    mjson_printf(fn, fnd, ",%Q:[", "cache_scores");
    off = 0;
    int first = 1;
    for (int i = 0; i < count; i++) {
        if (off + 6 > TSKC_SIZE) break;
        uint16_t stride = *(uint16_t *)(base + off + 2);
        if (stride < 10 || stride > 256) break;
        int numGames = base[off + 5];
        for (int g = 0; g < numGames; g++) {
            uint32_t rec = off + TSKC_GAMEREC_OFF + (uint32_t)g * 12u;
            if (rec + 11 >= TSKC_SIZE) break;
            uint16_t packed = *(uint16_t *)(base + rec + 6);
            uint16_t agg    = *(uint16_t *)(base + rec + 8);
            uint16_t pen    = *(uint16_t *)(base + rec + 10);
            if (!first) mjson_printf(fn, fnd, "%s", ",");
            first = 0;
            mjson_printf(fn, fnd, "%u,%u,%u",
                         (unsigned)packed, (unsigned)agg, (unsigned)pen);
        }
        off += stride;
    }
    mjson_printf(fn, fnd, "%s", "]");
}

static void fixup_cache_strides_on_load(const char *buf, int buflen)
{
    uint8_t *base = _teamSelectionKeyCache;

    /* Pass 1: re-lay strides host-native (must come first — the score
     * walk needs correct strides + numGames). */
    const char *arr = NULL; int alen = 0;
    if (mjson_find(buf, buflen, "$.cache_strides", &arr, &alen) != MJSON_TOK_ARRAY)
        return;                          /* pre-v2 save: leave blob as-is */
    {
        int pos = 0, voff, vlen, vt;
        uint32_t off = 0;
        while ((pos = fast_array_next(arr, alen, pos, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_NUMBER) break;
            if (off + 4 > TSKC_SIZE) break;
            uint16_t stride = (uint16_t)parse_long_slice(arr + voff, vlen);
            *(uint16_t *)(base + off + 2) = stride;
            if (stride < 10 || stride > 256) break;
            off += stride;
        }
    }

    /* Pass 2: re-lay game-record score fields host-native, same walk order
     * as emit. cache_scores is flat (3 decimals per record). */
    const char *sarr = NULL; int salen = 0;
    if (mjson_find(buf, buflen, "$.cache_scores", &sarr, &salen) != MJSON_TOK_ARRAY)
        return;                          /* pre-v3 save: scores stay as blob */
    {
        int count = (int)(int16_t)_teamSelectionKeyCacheCount;
        int pos = 0, voff, vlen, vt;
        uint32_t off = 0;
        for (int i = 0; i < count; i++) {
            if (off + 6 > TSKC_SIZE) break;
            uint16_t stride = *(uint16_t *)(base + off + 2);
            if (stride < 10 || stride > 256) break;
            int numGames = base[off + 5];
            for (int g = 0; g < numGames; g++) {
                uint32_t rec = off + TSKC_GAMEREC_OFF + (uint32_t)g * 12u;
                if (rec + 11 >= TSKC_SIZE) break;
                long packed = 0, agg = 0, pen = 0;
                if ((pos = fast_array_next(sarr, salen, pos, &voff, &vlen, &vt)) == 0) goto done;
                packed = parse_long_slice(sarr + voff, vlen);
                if ((pos = fast_array_next(sarr, salen, pos, &voff, &vlen, &vt)) == 0) goto done;
                agg = parse_long_slice(sarr + voff, vlen);
                if ((pos = fast_array_next(sarr, salen, pos, &voff, &vlen, &vt)) == 0) goto done;
                pen = parse_long_slice(sarr + voff, vlen);
                *(uint16_t *)(base + rec + 6)  = (uint16_t)packed;
                *(uint16_t *)(base + rec + 8)  = (uint16_t)agg;
                *(uint16_t *)(base + rec + 10) = (uint16_t)pen;
            }
            off += stride;
        }
    done:;
    }
}

/* ---------- Cross-target competition-buffer override ----------
 *
 * The competition working buffer (competitionFileBuffer = a stored copy
 * of diyCompetitionStart, typed DIYCompetitionEx) is saved byte-for-byte
 * by the diy_team_id_list_type_0/1/2 + *_competition_context blobs. Those
 * raw blobs are correct SAME-target but wrong CROSS-target for two
 * reasons (same class as the header bug fixed via the primary_comp_* runs):
 *
 *   1. The blobs are anchored at the OVERLAY named globals
 *      (diyTeamIdListType*), whose 64-bit byte positions are +4 vs the
 *      DIYCompetitionEx struct image the bytes actually follow (the
 *      overlay compensated namePtr's +4 but not the struct's +4 trailing
 *      alignment pad — verified: struct cupTeamIds 0x24E/0x256, overlay
 *      global 0x24E/0x252). So a 64-bit reader of a struct field lands 4
 *      bytes off from where the blob wrote it.
 *   2. The league standings rows hold a layout-dependent int32
 *      `teamSelOffset` (a byte distance from selTeamsPtr — negative for
 *      the careerTeam, which lives below selTeamsPtr in BSS) plus 7
 *      host-native uint16 stats. Neither survives a 32/64-bit + LE/BE
 *      boundary as raw bytes.
 *
 * Rather than re-plumb every blob, we keep them (same-target fidelity +
 * clean VERIFY) and ADD a typed override object that re-emits ONLY the
 * cross-target-sensitive fields, anchored at offsetof(DIYCompetitionEx,…)
 * so each lands at the host's real position, with teamSelOffset saved
 * SYMBOLICALLY as a team index. On load the override is applied AFTER the
 * raw blobs, correcting the struct-correct positions over the blob bytes.
 * Same-target: the override re-writes identical values, so VERIFY stays
 * clean. Cross-target: it overwrites the blob's mis-positioned garbage.
 *
 * PHASE 1 (here): LEAGUE standings only (competitionType == 0) — the
 * observed failing case. Cup (cupTeamIds/teamGridPairings + the int32
 * bracketScratch.teamOffsetTable) and the 5 per-slot *_competition_context
 * buffers are the same pattern and are the validated-next step; they are
 * NOT yet covered (see project_crosstarget_save_scope memory).
 *
 * teamSelOffset <-> identity: off = (uint8_t*)teamPtr - &selTeamsPtr;
 * stride = sizeof(TeamFileEx) = 684. A regular team row is index = off/684
 * (>= 0). The careerTeam row is a NEGATIVE off (careerTeam sits below
 * selTeamsPtr); we tag it with the sentinel -1 and re-derive its live
 * offset on load from &careerTeam. Empty/unused rows keep off == 0 → we
 * emit index 0 only when the row's identity is actually a team; the
 * untouched rows are already correct from the blob, so the override
 * emits ALL 64 rows verbatim (index or sentinel) to stay byte-exact. */

#define DIY_OVR_LEAGUE_ROWS 64

/* Local copy of diy_competition.h's diy_standings_row (not included here
 * to avoid that header's wider include graph). The standings are now the
 * relocated `leagueStandings[64]` field (step-3 untangle moved them off the
 * cupTeamIds[51] overlay) — see diy_competition.h. Field access, so it
 * auto-adjusts for the 32/64-bit struct growth. */
static inline DIYTeamStats *diy_ovr_row(DIYCompetitionEx *comp, int idx)
{
    return &comp->leagueStandings[idx];
}

/* teamSelOffset / teamOffsetTable now hold a stable team IDENTITY at runtime
 * (-1 == careerTeam, >= 0 == selTeamsPtr index) as of step-4 phase 3 — the
 * identity conversion moved into diy_team_to_identity / diy_resolve_team
 * (diy_competition.c). The JSON format was already identity-based, so these
 * save-seam converters are now pass-throughs; they no longer reference
 * careerTeam's address, which is what lets careerTeam leave the overlay.
 * (Kept as named functions so the emit/load call sites are unchanged, and so
 * loading a pre-phase-3 save still works — its JSON already stored identities.) */
static int32_t diy_teamseloffset_to_identity(int32_t v) { return v; }
static int32_t diy_identity_to_teamseloffset(int32_t v) { return v; }

/* ---------- Unified typed competition-slot serializer ----------
 *
 * A competition slot (competitionFileBuffer or any of the 4 *Context
 * buffers) is a DIYCompetitionEx. The raw byte blobs that currently carry
 * these regions are NOT cross-target safe (endian-swapped uint16s + the
 * 64-bit +8 struct growth mis-positioning every field past namePtr). The
 * fix that kills the whole bug class: serialize the slot through the TYPED
 * struct view — read/write each field by struct accessor so it lands at
 * the host's real offset, numbers as endian-neutral decimals.
 *
 * emit_comp_slot / load_comp_slot below do exactly that for the
 * cross-target-sensitive header + team-id region that FindTeamInDIY /
 * GetMoreExpensiveTeamsCount read (numTeams, competitionType, the int16
 * state run, and the per-type team-id array). The load runs AFTER the raw
 * blob, overwriting the struct-correct positions — same-target it rewrites
 * identical values (VERIFY clean), cross-target it corrects them.
 *
 * Migration: kept in parallel with the raw blobs during transition. Once
 * all 5 slots are covered and VERIFY is clean, the diy_team_id_list_type_*
 * and *_competition_context blobs (and the primary_comp_* runs + this
 * override's league_rows) get retired. namePtr is NOT serialized — it's
 * re-derived for all 5 slots on load by RebuildPointersAfterLoad Step 1b
 * (repointed at competitionName).
 *
 * Field set mirrors the proven primary_comp_* runs, generalized to take a
 * base pointer. competitionName is emitted too (self-contained slot
 * object), though it lives before namePtr so it was already byte-stable. */
static void emit_comp_slot(mjson_print_fn_t fn, void *fnd,
                           const char *key, const DIYCompetitionEx *s)
{
    const DIYCompetition *b = &s->base;
    mjson_printf(fn, fnd, "%Q:{", key);

    /* Scalar header fields — all endian-neutral decimals. */
    mjson_printf(fn, fnd,
        "%Q:%d,%Q:%u,"
        "%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,"
        "%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
        "%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
        "%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
        "%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
        "%Q:%u,%Q:%u,%Q:%u",
        "type",      (int)b->competitionType,
        "is_active", (unsigned)b->isActive,
        "comp_slot", (unsigned)b->compSlot,
        "slot_id",   (unsigned)b->slotId,
        "field_2e",  (unsigned)b->field_2E,
        "view_phase",(unsigned)b->viewRandomPhase,
        "avg_skill", (unsigned)b->avgSkill,
        "str_tier",  (unsigned)b->strengthTier,
        "num_teams", (int)b->numTeams,
        "pitch_type_or_season", (int)b->pitchTypeOrSeason,
        "pitch_type",(int)b->pitchType,
        "start_month",(int)b->startMonth,
        "end_month", (int)b->endMonth,
        "result_code",(int)b->resultCode,
        "subs_count",(int)b->substitutesCount,
        "slot_state_a",(int)b->slotStateA,
        "slot_state_b",(int)b->slotStateB,
        "slot_state_c",(int)b->slotStateC,
        "match_started",(int)b->matchStarted,
        "career_team_involved",(int)b->careerTeamInvolved,
        "has_groups",(int)b->hasGroups,
        "menu_state",(int)b->menuState,
        "round_iterator",(int)b->roundIterator,
        "num_teams_in_stage",(int)b->numTeamsInStage,
        "match_pairs_per_round",(int)b->matchPairsPerRound,
        "total_matches_in_round",(int)b->totalMatchesInRound,
        "round_complete",(int)b->roundComplete,
        "competition_finished",(int)b->competitionFinished,
        "current_round_index",(int)b->currentRoundIndex,
        "num_rounds",(int)b->numRounds,
        "sub_round_counter",(int)b->subRoundCounter,
        "play_each_team",(int)b->playEachTeam,
        "min_subs",  (unsigned)s->minSubstitutes,
        "points_for_win",(unsigned)s->pointsForAWin);

    /* compId(0x2C)/slotId(0x2D) is a HOST-NATIVE uint16 word, not two
     * independent bytes: the game writes it whole (*(uint16_t*)&compSlot = 0/1/2/
     * slot, diy_competition.c:447/692/1242, career_status.c:4283/4363) and
     * reads it whole (*(uint16_t*)&compSlot, career_status.c:1100). The separate
     * comp_id/slot_id bytes above capture the SAVING host's byte decomposition,
     * which rebuilds the WRONG word cross-endian (LE save slot=1 → compId=1,
     * slotId=0 → BE reads (1<<8)=256 → compType ∉ {0..4} →
     * UpdateCareerFinancesAfterMatch panic 0x9200). Emit the logical word
     * endian-neutral; load_comp_slot restores it as a word AFTER the bytes. */
    mjson_printf(fn, fnd, ",%Q:%u", "comp_slot_word",
                 (unsigned)*(const uint16_t *)&b->compSlot);

    /* competitionName[36] — char string. */
    mjson_printf(fn, fnd, "%s", ",");
    emit_string_field(fn, fnd, "name",
                      (const uint8_t *)b->competitionName,
                      sizeof(b->competitionName));

    /* Per-type team-id array. Stored as BE byte-pairs {country,ordinal}
     * (read via BE_READ_U16 on both hosts → endian-safe content), but
     * positioned via the struct field so the cross-target offset is right.
     *
     * COUNT = numTeams, NOT sizeof(field). The struct declares
     * leagueTeamIds[14] / cupTeamIds[64], but those declared sizes are
     * NOT the real bounds: an N-team competition writes N entries from the
     * field base, and the read sites index up to teamIndex = N-1 (e.g.
     * DIY_CachePrimaryQualifiedTeamsGrid reads leagueTeamIds[standings-
     * sorted index] for any of the N teams). An 18-team league writes 18
     * entries (36 B) past the declared [14] (28 B) — sizeof(field)=14
     * truncated entries 14..17, leaving them to fall back to the endian-
     * broken raw diy_team_id_list blob → garbage ids → VerifyTeamLoaded
     * panic cross-target. Reading numTeams entries from the flat slot
     * buffer covers exactly what the game uses. Clamp to 64 (max teams) as
     * a runaway guard. Tournament (type 2) reads a sibling
     * diyFileBufferCopy region — not covered here; emit empty. */
    {
        const uint8_t *tids = NULL;
        if (b->competitionType == 0)
            tids = (const uint8_t *)s->matchData.league.leagueTeamIds;
        else if (b->competitionType == 1)
            tids = (const uint8_t *)s->cupTeamIds;
        int16_t nt = b->numTeams;
        if (nt < 0) nt = 0; else if (nt > 64) nt = 64;
        uint32_t tlen = (uint32_t)nt * 2u;       /* bytes: 2 per team id */
        mjson_printf(fn, fnd, ",%Q:[", "team_ids");
        if (tids) {
            for (uint32_t i = 0; i < tlen; i++) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%u", (unsigned)tids[i]);
            }
        }
        mjson_printf(fn, fnd, "%s", "]");
    }

    /* matchData union (0xEA bytes) — struct-anchored byte blob. This is the
     * WHOLE 0x64.. region whose struct offset shifts +8 cross-target (0x64
     * on 32-bit, 0x6C on 64-bit, from the earlier namePtr widening). The raw
     * competition_context blob writes it at the SAVING host's offset, so a
     * cross-target load reads every matchData field 8 bytes off — the
     * cup-bracket "played" flags (cupRoundResults[0..0x3F]), the knockout
     * scores (cupRoundResults[0x40]), leagueTeamIds, teamEntryOffsets, round
     * info, etc. all misplaced (live-confirmed: same CUP.CAR shows different
     * cup scores + played/unplayed flags on 64-bit vs 32-bit). Emitting the
     * whole union struct-anchored fixes the LAYOUT for every field at once.
     * Pointer-free + identical 0xEA size on both targets, so a flat byte
     * copy is layout-correct. (Byte-order of the host-native u16 score
     * fields inside is handled separately, endian-neutral, by
     * cup_score_tab/agg/pen below — that layer sits on top of this one.)
     *
     * TYPE-2 EXCEPTION: a tournament reads this same byte region via
     * LITERAL offsets (diyTournamentTeamIds at buf+0x9A is inside it),
     * NOT via the matchData struct field. Struct-anchoring it would
     * re-lay those literal-offset bytes from the saving host's matchData
     * base to the loading host's — an 8-byte shift between 32/64-bit that
     * would BREAK the team-id list the flat raw blob already places
     * correctly (literal offsets are identical on both bitnesses). So
     * skip matchdata_raw for tournaments and let the flat blob carry the
     * region; only the uint16 slot fields need the endian-neutral re-lay
     * (tournament_slots, below). */
    if (b->competitionType != 2) {
        const uint8_t *md = (const uint8_t *)&s->matchData;
        mjson_printf(fn, fnd, ",%Q:[", "matchdata_raw");
        for (uint32_t i = 0; i < (uint32_t)sizeof(s->matchData); i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)md[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }

    /* LEAGUE teamEntryOffsets — standings-sorted positions (value/18 =
     * team index), host-native uint16, DYNAMIC (rewritten by
     * DIY_SortLeagueTableByStandings). Declared [14] but uses numTeams
     * entries (same overflow as leagueTeamIds): an 18-team league's
     * entries 14..17 fell outside the fixed primary_comp_matchdata_words
     * window and into the endian/position-fragile blob → a bad standings
     * index → DIY_CachePrimaryQualifiedTeamsGrid read a garbage team →
     * VerifyTeamLoaded panic. Emit numTeams host-native words (decimal,
     * endian-neutral). League only; cup view doesn't use this field. */
    if (b->competitionType == 0) {
        int16_t nt = b->numTeams;
        if (nt < 0) nt = 0; else if (nt > 64) nt = 64;
        const uint16_t *eo = s->matchData.league.teamEntryOffsets;
        mjson_printf(fn, fnd, ",%Q:[", "team_entry_offsets");
        for (int16_t i = 0; i < nt; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)eo[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }

    /* LEAGUE progress run — DIYCompetitionEx 0x1BE..0x24D. NOT covered by the
     * scalar header above (which stops at min_subs/points_for_win) nor by the
     * matchData blob (0x64..0x14D), so it rode the raw 64-bit-layout
     * competition_context blob: a 32-bit BE load read it 8 bytes shifted (the
     * DIYCompetition namePtr +8 growth) → garbage sectionMatchesProgress →
     * getCurrentFixtureTeamPtrs picked the wrong fixture → both teams classed
     * computer → PLAY MATCH hidden (live-confirmed: SDL=3, Amiga=8793). Emit
     * endian-neutral so it survives cross-target. League only: type-1 cup uses
     * phaseStartMonths as cup_score_pen, and type-2 tournament reuses this
     * region as slot data (tournament_slots). See
     * project_compslot_league_progress_crosstarget. */
    if (b->competitionType == 0) {
        mjson_printf(fn, fnd,
            ",%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u",
            "matches_played_count",        (unsigned)s->matchesPlayedCount,
            "league_current_slot",         (unsigned)s->leagueCurrentSlot,
            "league_match_progress",       (unsigned)s->leagueMatchProgress,
            "league_total_matches",        (unsigned)s->leagueTotalMatches,
            "section_half_group_count",    (unsigned)s->leagueSectionState.halfGroupCount,
            "section_matches_per_half",    (unsigned)s->leagueSectionState.matchesPerHalf,
            "section_half_groups_counter", (unsigned)s->leagueSectionState.halfGroupsCounter,
            "section_matches_progress",    (unsigned)s->leagueSectionState.sectionMatchesProgress);
        /* phaseStartMonths[128] — per-round month sort keys (cache builder).
         * Byte content is endian-neutral; only its struct offset shifts
         * cross-target, so struct-anchor it here. */
        mjson_printf(fn, fnd, ",%Q:[", "phase_start_months");
        for (int i = 0; i < 128; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u",
                         (unsigned)s->leagueSectionState.phaseStartMonths[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");

        /* matchData.league host-native uint16 SCALARS (parent 0x64,0x66,0x68,
         * 0x6A,0x6C). Like teamEntryOffsets they ride the matchData byte blob,
         * which struct-anchors the OFFSET but keeps the saving host's BYTE ORDER
         * — so each byte-swaps on a LE->BE cross-target load (leagueMaxSubstitutes
         * 5 -> 0x0500=1280 -> maxSubstitutesEntries garbage -> the career
         * play-match PLAY / VIEW RESULT button hidden; live-confirmed via gdb).
         * Emit endian-neutral; re-lay on load over the blob.
         *
         * DO NOT add competitionFormat(0x14A)/roundFormat(0x14C) here: for an
         * N>14-team league the leagueTeamIds[14] array OVERFLOWS its declared
         * [14] bound and entries 14/15 land at EXACTLY 0x14A/0x14C — i.e. those
         * two "fields" ARE leagueTeamIds[14]/[15], which are endian-stable BE
         * pairs handled by team_ids. Serializing them as host-native uint16
         * byte-swapped the team-id bytes (leagueTeamIds[14] 0x142a -> 0x2a14)
         * -> VerifyTeamLoaded panic in GetMoreExpensiveTeamsCount (gdb-confirmed
         * on an 18-team league). leagueTeamIds(0x12E) = team_ids (BE pairs);
         * teamEntryOffsets handled above; cupRoundResults/shuffleAndPad are byte
         * arrays. See project_compslot_matchdata_uint16_crosstarget. */
        mjson_printf(fn, fnd,
            ",%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u",
            "league_min_substitutes",          (unsigned)s->matchData.league.leagueMinSubstitutes,
            "league_max_substitutes",          (unsigned)s->matchData.league.leagueMaxSubstitutes,
            "league_has_groups_diy",           (unsigned)s->matchData.league.hasGroupsDiy,
            "league_matches_per_section",      (unsigned)s->matchData.league.totalMatchesPerSection,
            "league_matches_per_section_live", (unsigned)s->matchData.league.totalMatchesPerSectionLive);
    }

    /* CUP bracketScratch.teamOffsetTable[64] — int32 byte-offsets from
     * selTeamsPtr to each bracket team (read as `&selTeamsPtr + off` in
     * career_teams.c:2040/2050, diy_competition.c:2903/6282). Raw FLD_BYTES
     * byte-swapped each int32 cross-target → garbage team pointer → panic
     * the moment a cup match auto-sims. Layout-DEPENDENT (a BSS distance),
     * so even byte-correct it wouldn't be valid on a different build —
     * emit SYMBOLICALLY as team indices (off/684, careerTeam=-1), the same
     * encoding as the league standings teamSelOffset. Indexed by grid slot
     * (0..63, sparse) so emit the full 64-entry table verbatim; unused
     * slots stay 0 → identity 0, harmless (never read for absent teams).
     * League view doesn't use this field. */
    if (b->competitionType == 1) {
        const int32_t *ot = s->bracketScratch.teamOffsetTable;
        mjson_printf(fn, fnd, ",%Q:[", "team_offset_table");
        for (int i = 0; i < 64; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%d",
                         (int)diy_teamseloffset_to_identity(ot[i]));
        }
        mjson_printf(fn, fnd, "%s", "]");

        /* CUP teamGridPairings[64] — uint8 grid-slot indices; the cup
         * bracket reads slot1/slot2 = teamGridPairings[currentMatchIndex*2
         * .. +1] then names via teamOffsetTable[slotN]. Byte-safe content,
         * BUT its struct offset shifts +8 cross-target (0x2CE on 32-bit,
         * 0x2D6 on 64-bit — the namePtr widening earlier in DIYCompetitionEx).
         * The raw competition_context blob is a flat copy at 64-bit offsets,
         * so a 32-bit load read teamGridPairings 8 bytes early → 8 leading
         * zeros → slot1=slot2=0 → "TEAM v TEAM" (live-gdb-confirmed: the
         * DUSSELDORF-v-DUSSELDORF cup bracket). Emit struct-anchored here so
         * offsetof places it right on each host. */
        const uint8_t *gp = s->teamGridPairings;
        mjson_printf(fn, fnd, ",%Q:[", "team_grid_pairings");
        for (int i = 0; i < 64; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)gp[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");

        /* CUP knockout score arrays — three parallel int16 arrays indexed
         * by match (diy_competition.c:2515-2517), HOST-NATIVE so they
         * byte-swap cross-target (live-confirmed: bracket scores scrambled,
         * HANSA 1-2 shown as 2-3). Emit endian-neutral as int16 decimals:
         *   scoreTab1   = cupRoundResults[0x40]  (knockout score)
         *   aggregate   = _bss_2E0AE[0]          (2nd-leg aggregate)
         *   penalty     = phaseStartMonths[0]    (penalty score)
         * 18 entries each (scoreTab1's window cupRoundResults[0x40..0x64]
         * = 36 bytes = 18 words; round match count <= 18). The surrounding
         * byte regions ride the raw blob unchanged; only these int16 score
         * sub-arrays need endian-neutral re-lay. */
        {
            const int16_t *st = (const int16_t *)&s->matchData.cup.cupRoundResults[0x40];
            const int16_t *ag = (const int16_t *)s->cupAggregateScores;
            const int16_t *pn = (const int16_t *)&s->leagueSectionState.phaseStartMonths[0];
            mjson_printf(fn, fnd, ",%Q:[", "cup_score_tab");
            for (int i = 0; i < 18; i++) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%d", (int)st[i]);
            }
            mjson_printf(fn, fnd, "%s", "]");
            mjson_printf(fn, fnd, ",%Q:[", "cup_score_agg");
            for (int i = 0; i < 18; i++) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%d", (int)ag[i]);
            }
            mjson_printf(fn, fnd, "%s", "]");
            mjson_printf(fn, fnd, ",%Q:[", "cup_score_pen");
            for (int i = 0; i < 18; i++) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%d", (int)pn[i]);
            }
            mjson_printf(fn, fnd, "%s", "]");
        }

        /* CUP round-config + bracket-state uint16s, plus the per-round config
         * arrays (roundLegs/roundExtraTime/roundPenalties[6] @ parent 0x66/0x72/
         * 0x7E). All ride the matchData/context byte blob, so each byte-swaps on a
         * LE->BE cross-target load (roundLegs 2 -> 0x0200 -> roundFormat derives to
         * 512 -> the ==2 two-leg test fails -> round collapses to single-leg;
         * verified live on the Amiga 2026-06-10 via TEST.DIY: SDL-LE save -> BE
         * load read roundLegs=2, not 512). Emit endian-neutral; re-lay on load.
         * (Cup team list is cupTeamIds@0x24E, so leagueTeamIds[14]/[15] never
         * overflow into 0x14A/0x14C here — unlike the LEAGUE view.)
         *
         * Of these, only roundLegs/roundExtraTime/roundPenalties genuinely PERSIST;
         * the 8 scalars (currentMatchIndex, competitionFormat, roundFormat,
         * secondLegCounter, groupSectionFlag, matchCount, totalMatches,
         * curMonthIdx) are RESET/RE-DERIVED at cup setup (diy_competition.c:
         * competitionFormat/counters 1673/1687-1690, roundFormat<-roundLegs 1677,
         * totalMatches<-numTeams 1755 via DIY_RecomputeCompetitionDerivedFields,
         * which runs on load) so today they round-trip as 0/derived regardless.
         * We KEEP serializing them as a safety margin while the JSON save migration
         * (roadmap step 4) is incomplete: should a field stop being recomputed, or
         * be read before setup, the saved value is already correct cross-target.
         * Revisit (consider trimming) once the JSON format is the sole source of
         * truth — tracked in docs/SAVE_FORMAT_TRIM_CANDIDATES.md. (Trim 02e7810
         * reverted on this basis 2026-06-10.) See
         * project_compslot_cup_uint16_crosstarget. */
        mjson_printf(fn, fnd,
            ",%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u",
            "cup_current_match_index", (unsigned)s->matchData.cup.currentMatchIndex,
            "cup_competition_format",  (unsigned)s->matchData.cup.competitionFormat,
            "cup_round_format",        (unsigned)s->matchData.cup.roundFormat,
            "cup_second_leg_counter",  (unsigned)s->bracketScratch.secondLegCounter,
            "cup_group_section_flag",  (unsigned)s->bracketScratch.groupSectionFlag,
            "cup_match_count",         (unsigned)s->bracketScratch.matchCount,
            "cup_total_matches",       (unsigned)s->bracketScratch.totalMatches,
            "cup_cur_month_idx",       (unsigned)s->bracketScratch.curMonthIdx);
        mjson_printf(fn, fnd, ",%Q:[", "cup_round_legs");
        for (int i = 0; i < 6; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)s->matchData.cup.roundLegs[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");
        mjson_printf(fn, fnd, ",%Q:[", "cup_round_extra_time");
        for (int i = 0; i < 6; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)s->matchData.cup.roundExtraTime[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");
        mjson_printf(fn, fnd, ",%Q:[", "cup_round_penalties");
        for (int i = 0; i < 6; i++) {
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd, "%u", (unsigned)s->matchData.cup.roundPenalties[i]);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }

    /* TOURNAMENT (type 2) slot records — the 6-element DIYTournamentSlot
     * array embedded at slot-base +0x15A (stride 0x76). Unlike the cup/
     * league matchData fields, the tournament layout is reached by the
     * game via LITERAL raw offsets (diy_competition.c reads `buf + 0x15A`
     * on both targets), so the array sits at the SAME byte offset on 32-
     * and 64-bit — there is no +8 namePtr shift to anchor against here.
     * The cross-target hazard is byte ORDER: each slot has 13 host-native
     * uint16 fields (doubleCount..matchesPerSlot, struct offsets 0x02..
     * 0x1A) that the raw context blob dumps in saving-host order and so
     * byte-swap on a LE<->BE load. Emit them endian-neutral (decimal),
     * mirroring the cup cup_score / team_grid_pairings treatment.
     *
     * The 2-byte pad (slot 0x00) and the 90-byte progressionTable (slot
     * 0x1C) are byte arrays — they ride the raw blob unchanged. The
     * tournament team-id list (+0x9A, diyTournamentTeamIds) is BE-packed
     * byte pairs read via BE_READ_U16, so its content is already endian-
     * identical on both hosts and stays on the raw blob too; likewise the
     * 64-byte shuffle index table (+0x5A) is plain bytes. Only the slot
     * uint16 fields need re-laying.
     *
     * The current-stage league/cup standings live in the relocated
     * leagueStandings[] tail of the stageSnapshot image. emit_comp_slot
     * here does NOT cover them — the primary's are serialized by
     * stage_standings (below) and each *Context slot's by emit_ctx_stage. */
    if (b->competitionType == 2) {
        const DIYTournamentSlot *slots =
            (const DIYTournamentSlot *)((const uint8_t *)s + 0x15A);
        mjson_printf(fn, fnd, ",%Q:[", "tournament_slots");
        for (int si = 0; si < 6; si++) {
            const DIYTournamentSlot *sl = &slots[si];
            if (si > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd,
                "{%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,"
                "%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u}",
                "double_count",     (unsigned)sl->doubleCount,
                "is_league",        (unsigned)sl->isLeagueFormat,
                "seeding",          (unsigned)sl->seedingType,
                "num_teams",        (unsigned)sl->numTeams,
                "division_factor",  (unsigned)sl->divisionFactor,
                "play_each_team",   (unsigned)sl->playEachTeam,
                "init_168",         (unsigned)sl->initConst168,
                "groups",           (unsigned)sl->groups,
                "teams_per_group",  (unsigned)sl->teamsPerGroup,
                "format_16e",       (unsigned)sl->formatFlag16E,
                "init_170",         (unsigned)sl->initConst170,
                "format_172",       (unsigned)sl->formatFlag172,
                "matches_per_slot", (unsigned)sl->matchesPerSlot);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }

    mjson_printf(fn, fnd, "%s", "}");
}

/* Load a comp slot emitted by emit_comp_slot over the struct at `s`
 * (typically already populated by the raw blob load). `parent`/`plen` is
 * the enclosing JSON object; `key` selects the slot sub-object. */
static void load_comp_slot(const char *parent, int plen,
                           const char *key, DIYCompetitionEx *s)
{
    char path[64];
    swos_io_snprintf(path, sizeof(path), "$.%s", key);
    const char *o = NULL; int olen = 0;
    if (mjson_find(parent, plen, path, &o, &olen) != MJSON_TOK_OBJECT)
        return;                          /* slot absent (older save) */

    DIYCompetition *b = &s->base;
    long v;
#define LD16(jk, fld) \
    if (mjson_get_long(o, olen, "$." jk, &v) > 0) (fld) = (int16_t)v
#define LD8(jk, fld) \
    if (mjson_get_long(o, olen, "$." jk, &v) > 0) (fld) = (uint8_t)v
#define LDU16(jk, fld) \
    if (mjson_get_long(o, olen, "$." jk, &v) > 0) (fld) = (uint16_t)v

    LD16("type", b->competitionType);
    LDU16("is_active", b->isActive);
    LD8("comp_slot", b->compSlot);
    LD8("slot_id", b->slotId);
    LD8("field_2e", b->field_2E);
    /* compSlot/slotId is a host-native word (see emit_comp_slot). The bytes
     * above are the saving host's decomposition — wrong cross-endian. Override
     * with the logical word so it round-trips on BE. New-save key; absent in
     * old saves (then the bytes stand, same-target-correct). MUST run after the
     * two LD8s since it overwrites both bytes. */
    if (mjson_get_long(o, olen, "$.comp_slot_word", &v) > 0)
        *(uint16_t *)&b->compSlot = (uint16_t)v;
    LD8("view_phase", b->viewRandomPhase);
    LD8("avg_skill", b->avgSkill);
    LD8("str_tier", b->strengthTier);
    LD16("num_teams", b->numTeams);
    LD16("pitch_type_or_season", b->pitchTypeOrSeason);
    LD16("pitch_type", b->pitchType);
    if (mjson_get_long(o, olen, "$.start_month", &v) > 0) b->startMonth = (int8_t)v;
    if (mjson_get_long(o, olen, "$.end_month", &v) > 0)   b->endMonth   = (int8_t)v;
    LD16("result_code", b->resultCode);
    LD16("subs_count", b->substitutesCount);
    LD16("slot_state_a", b->slotStateA);
    LD16("slot_state_b", b->slotStateB);
    LD16("slot_state_c", b->slotStateC);
    LD16("match_started", b->matchStarted);
    LD16("career_team_involved", b->careerTeamInvolved);
    LD16("has_groups", b->hasGroups);
    LD16("menu_state", b->menuState);
    LD16("round_iterator", b->roundIterator);
    LD16("num_teams_in_stage", b->numTeamsInStage);
    LD16("match_pairs_per_round", b->matchPairsPerRound);
    LD16("total_matches_in_round", b->totalMatchesInRound);
    LD16("round_complete", b->roundComplete);
    LD16("competition_finished", b->competitionFinished);
    LD16("current_round_index", b->currentRoundIndex);
    LD16("num_rounds", b->numRounds);
    LD16("sub_round_counter", b->subRoundCounter);
    LD16("play_each_team", b->playEachTeam);
    LDU16("min_subs", s->minSubstitutes);
    LDU16("points_for_win", s->pointsForAWin);
#undef LD16
#undef LD8
#undef LDU16

    load_string_field(o, olen, "$.name",
                      (uint8_t *)b->competitionName,
                      sizeof(b->competitionName));

    /* Team-id byte array → the per-type slot field. COUNT = numTeams*2
     * (NOT sizeof the declared field — the game writes numTeams entries
     * past the declared [14]/[64]; see emit_comp_slot). numTeams was
     * loaded above. parse_byte_array_slice caps at tlen, so a longer JSON
     * array can't overrun. The bytes land in the flat slot buffer from the
     * field base, matching the runtime write. */
    {
        uint8_t *tids = NULL;
        if (b->competitionType == 0)
            tids = (uint8_t *)s->matchData.league.leagueTeamIds;
        else if (b->competitionType == 1)
            tids = (uint8_t *)s->cupTeamIds;
        int16_t nt = b->numTeams;
        if (nt < 0) nt = 0; else if (nt > 64) nt = 64;
        uint32_t tlen = (uint32_t)nt * 2u;
        if (tids && tlen) {
            const char *arr = NULL; int alen = 0;
            if (mjson_find(o, olen, "$.team_ids", &arr, &alen) == MJSON_TOK_ARRAY)
                parse_byte_array_slice(arr, alen, tids, tlen);
        }
    }

    /* matchData union — struct-anchored byte blob (fixes the +8 cross-target
     * layout shift for the whole 0xEA region; see emit). Loaded BEFORE the
     * endian-neutral cup_score_* / teamEntryOffsets fixups below so those
     * layer their host-native values on top. Skipped for tournaments
     * (type 2) — see the emit-side TYPE-2 EXCEPTION note. */
    if (b->competitionType != 2) {
        const char *mr = NULL; int mrl = 0;
        if (mjson_find(o, olen, "$.matchdata_raw", &mr, &mrl) == MJSON_TOK_ARRAY)
            parse_byte_array_slice(mr, mrl, (uint8_t *)&s->matchData,
                                   (uint32_t)sizeof(s->matchData));
    }

    /* LEAGUE teamEntryOffsets — host-native uint16 words, numTeams of them
     * (see emit). parse_u16_array_slice writes host-native (endian-neutral
     * on disk). Caps at numTeams*2 bytes so it can't overrun. */
    if (b->competitionType == 0) {
        int16_t nt = b->numTeams;
        if (nt < 0) nt = 0; else if (nt > 64) nt = 64;
        uint32_t elen = (uint32_t)nt * 2u;
        if (elen) {
            const char *arr = NULL; int alen = 0;
            if (mjson_find(o, olen, "$.team_entry_offsets", &arr, &alen)
                    == MJSON_TOK_ARRAY)
                parse_u16_array_slice(arr, alen,
                    (uint8_t *)s->matchData.league.teamEntryOffsets, elen);
        }
    }

    /* LEAGUE progress run (0x1BE..0x24D) — endian-neutral re-lay over the raw
     * blob's cross-target-shifted bytes; see emit. Old saves lacking these
     * keys leave the prior (blob/recomputed) values, defaulting to 0. */
    if (b->competitionType == 0) {
        if (mjson_get_long(o, olen, "$.matches_played_count", &v) > 0)        s->matchesPlayedCount = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_current_slot", &v) > 0)         s->leagueCurrentSlot = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_match_progress", &v) > 0)       s->leagueMatchProgress = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_total_matches", &v) > 0)        s->leagueTotalMatches = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.section_half_group_count", &v) > 0)    s->leagueSectionState.halfGroupCount = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.section_matches_per_half", &v) > 0)    s->leagueSectionState.matchesPerHalf = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.section_half_groups_counter", &v) > 0) s->leagueSectionState.halfGroupsCounter = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.section_matches_progress", &v) > 0)    s->leagueSectionState.sectionMatchesProgress = (uint16_t)v;
        const char *arr = NULL; int alen = 0;
        if (mjson_find(o, olen, "$.phase_start_months", &arr, &alen) == MJSON_TOK_ARRAY)
            parse_byte_array_slice(arr, alen, s->leagueSectionState.phaseStartMonths, 128);
        /* matchData.league host-native uint16 scalars — re-lay over the blob
         * (see emit). Old saves lacking these keys keep the (cross-target-broken)
         * blob values. */
        if (mjson_get_long(o, olen, "$.league_min_substitutes", &v) > 0)          s->matchData.league.leagueMinSubstitutes = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_max_substitutes", &v) > 0)          s->matchData.league.leagueMaxSubstitutes = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_has_groups_diy", &v) > 0)           s->matchData.league.hasGroupsDiy = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_matches_per_section", &v) > 0)      s->matchData.league.totalMatchesPerSection = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.league_matches_per_section_live", &v) > 0) s->matchData.league.totalMatchesPerSectionLive = (uint16_t)v;
        /* NOT competition_format/round_format: they alias leagueTeamIds[14]/[15]
         * for N>14-team leagues (BE pairs handled by team_ids) — loading them as
         * host-native uint16 byte-swapped the team id -> VerifyTeamLoaded panic.
         * Ignoring any legacy key here lets the blob/team_ids BE-pair bytes stand,
         * which also un-corrupts saves written by the buggy interim build. */
    }

    /* CUP teamOffsetTable[64] — symbolic team indices → restore to live
     * selTeamsPtr byte-offsets (see emit). Walk the 64-element array; each
     * decimal is a team index (or -1 careerTeam sentinel) converted back
     * via diy_identity_to_teamseloffset. */
    if (b->competitionType == 1) {
        const char *arr = NULL; int alen = 0;
        if (mjson_find(o, olen, "$.team_offset_table", &arr, &alen)
                == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt, i = 0;
            while (i < 64 &&
                   (pos = fast_array_next(arr, alen, pos,
                                          &voff, &vlen, &vt)) != 0) {
                if (vt == MJSON_TOK_NUMBER) {
                    int32_t id = (int32_t)parse_long_slice(arr + voff, vlen);
                    s->bracketScratch.teamOffsetTable[i] =
                        diy_identity_to_teamseloffset(id);
                }
                i++;
            }
        }

        /* CUP teamGridPairings[64] — uint8, struct-anchored (corrects the
         * +8 cross-target offset shift; see emit). Walk up to 64 bytes. */
        const char *garr = NULL; int galen = 0;
        if (mjson_find(o, olen, "$.team_grid_pairings", &garr, &galen)
                == MJSON_TOK_ARRAY)
            parse_byte_array_slice(garr, galen, s->teamGridPairings, 64);

        /* CUP knockout score arrays — endian-neutral int16, struct-anchored
         * (see emit). parse_u16_array_slice writes host-native; 18 words =
         * 36 bytes each. */
        struct { const char *key; uint8_t *dst; } sc[3] = {
            { "$.cup_score_tab", (uint8_t *)&s->matchData.cup.cupRoundResults[0x40] },
            { "$.cup_score_agg", (uint8_t *)s->cupAggregateScores },
            { "$.cup_score_pen", (uint8_t *)&s->leagueSectionState.phaseStartMonths[0] },
        };
        for (int k = 0; k < 3; k++) {
            const char *sa = NULL; int sl = 0;
            if (mjson_find(o, olen, sc[k].key, &sa, &sl) == MJSON_TOK_ARRAY)
                parse_u16_array_slice(sa, sl, sc[k].dst, 36);
        }

        /* CUP round-config + bracket-state uint16s + per-round config arrays —
         * endian-neutral re-lay over the blob (see emit). The 8 scalars are
         * recomputed at cup setup, so they are belt-and-suspenders kept during the
         * JSON migration; the 3 arrays are the genuine persisted design data. Old
         * saves lacking these keys keep the blob values. parse_u16_array_slice
         * writes host-native; cap 12 bytes = 6 words. */
        if (mjson_get_long(o, olen, "$.cup_current_match_index", &v) > 0) s->matchData.cup.currentMatchIndex = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_competition_format", &v) > 0)  s->matchData.cup.competitionFormat  = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_round_format", &v) > 0)        s->matchData.cup.roundFormat        = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_second_leg_counter", &v) > 0)  s->bracketScratch.secondLegCounter  = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_group_section_flag", &v) > 0)  s->bracketScratch.groupSectionFlag  = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_match_count", &v) > 0)         s->bracketScratch.matchCount        = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_total_matches", &v) > 0)       s->bracketScratch.totalMatches      = (uint16_t)v;
        if (mjson_get_long(o, olen, "$.cup_cur_month_idx", &v) > 0)       s->bracketScratch.curMonthIdx       = (uint16_t)v;
        {
            const char *ra = NULL; int ral = 0;
            if (mjson_find(o, olen, "$.cup_round_legs", &ra, &ral) == MJSON_TOK_ARRAY)
                parse_u16_array_slice(ra, ral, (uint8_t *)s->matchData.cup.roundLegs, 12);
            if (mjson_find(o, olen, "$.cup_round_extra_time", &ra, &ral) == MJSON_TOK_ARRAY)
                parse_u16_array_slice(ra, ral, (uint8_t *)s->matchData.cup.roundExtraTime, 12);
            if (mjson_find(o, olen, "$.cup_round_penalties", &ra, &ral) == MJSON_TOK_ARRAY)
                parse_u16_array_slice(ra, ral, (uint8_t *)s->matchData.cup.roundPenalties, 12);
        }
    }

    /* TOURNAMENT (type 2) slot records — inverse of the emit above. Walk
     * up to 6 objects in "tournament_slots" and write each one's 13 uint16
     * fields host-native into the DIYTournamentSlot array at slot-base
     * +0x15A. The 2-byte pad / progressionTable / team-ids / shuffle were
     * already restored by the raw context blob (byte-safe), so this only
     * re-lays the endian-sensitive uint16 fields on top. */
    if (b->competitionType == 2) {
        DIYTournamentSlot *slots =
            (DIYTournamentSlot *)((uint8_t *)s + 0x15A);
        const char *arr = NULL; int alen = 0;
        if (mjson_find(o, olen, "$.tournament_slots", &arr, &alen)
                == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt, si = 0;
            while (si < 6 &&
                   (pos = fast_array_next(arr, alen, pos,
                                          &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) { si++; continue; }
                const char *e = arr + voff; int el = vlen;
                DIYTournamentSlot *sl = &slots[si];
                long sv;
#define LDSLOT(jk, fld) \
    if (mjson_get_long(e, el, "$." jk, &sv) > 0) (fld) = (uint16_t)sv
                LDSLOT("double_count",     sl->doubleCount);
                LDSLOT("is_league",        sl->isLeagueFormat);
                LDSLOT("seeding",          sl->seedingType);
                LDSLOT("num_teams",        sl->numTeams);
                LDSLOT("division_factor",  sl->divisionFactor);
                LDSLOT("play_each_team",   sl->playEachTeam);
                LDSLOT("init_168",         sl->initConst168);
                LDSLOT("groups",           sl->groups);
                LDSLOT("teams_per_group",  sl->teamsPerGroup);
                LDSLOT("format_16e",       sl->formatFlag16E);
                LDSLOT("init_170",         sl->initConst170);
                LDSLOT("format_172",       sl->formatFlag172);
                LDSLOT("matches_per_slot", sl->matchesPerSlot);
#undef LDSLOT
                si++;
            }
        }
    }
}

/* Emit the league standings override object. Reads competitionFileBuffer
 * as DIYCompetitionEx; only meaningful when competitionType == 0. For
 * non-league comps we still emit the key with an empty array + the type,
 * so the loader can branch without guessing. */
static void emit_competition_overrides(mjson_print_fn_t fn, void *fnd)
{
    DIYCompetitionEx *comp = (DIYCompetitionEx *)&_primaryCompetitionBuf;
    int16_t type = comp->base.competitionType;

    mjson_printf(fn, fnd, "%Q:{", "competition_overrides");
    mjson_printf(fn, fnd, "%Q:%d", "type", (int)type);

    if (type == 0) {
        /* League: 64 standings rows. teamSelOffset → identity; 7 uint16
         * stats endian-neutral. */
        mjson_printf(fn, fnd, ",%Q:[", "league_rows");
        for (int i = 0; i < DIY_OVR_LEAGUE_ROWS; i++) {
            DIYTeamStats *r = diy_ovr_row(comp, i);
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd,
                "{%Q:%d,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u}",
                "id",  (int)diy_teamseloffset_to_identity(r->teamSelOffset),
                "gp",  (unsigned)r->gamesPlayed,
                "w",   (unsigned)r->wins,
                "d",   (unsigned)r->draws,
                "l",   (unsigned)r->losses,
                "gf",  (unsigned)r->goalsFor,
                "ga",  (unsigned)r->goalsAgainst,
                "pts", (unsigned)r->points);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }
    mjson_printf(fn, fnd, "%s", "}");
}

/* Apply the league standings override over whatever the raw blobs left in
 * competitionFileBuffer. Called AFTER load_scalar_table (which loads the
 * blobs) so it corrects the struct-correct positions. */
static void load_competition_overrides(const char *buf, int buflen)
{
    const char *obj = NULL; int obj_len = 0;
    if (mjson_find(buf, buflen, "$.competition_overrides",
                   &obj, &obj_len) != MJSON_TOK_OBJECT)
        return;                          /* older save without overrides */

    long type = 0;
    if (mjson_get_long(obj, obj_len, "$.type", &type) <= 0)
        return;
    if ((int)type != 0)
        return;                          /* phase 1: league only */

    const char *rows = NULL; int rows_len = 0;
    if (mjson_find(obj, obj_len, "$.league_rows",
                   &rows, &rows_len) != MJSON_TOK_ARRAY)
        return;

    DIYCompetitionEx *comp = (DIYCompetitionEx *)&_primaryCompetitionBuf;
    int pos = 0, voff, vlen, vt, i = 0;
    while (i < DIY_OVR_LEAGUE_ROWS &&
           (pos = fast_array_next(rows, rows_len, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) { i++; continue; }
        const char *e = rows + voff; int el = vlen;
        long id = 0, gp = 0, w = 0, d = 0, l = 0, gf = 0, ga = 0, pts = 0;
        mjson_get_long(e, el, "$.id",  &id);
        mjson_get_long(e, el, "$.gp",  &gp);
        mjson_get_long(e, el, "$.w",   &w);
        mjson_get_long(e, el, "$.d",   &d);
        mjson_get_long(e, el, "$.l",   &l);
        mjson_get_long(e, el, "$.gf",  &gf);
        mjson_get_long(e, el, "$.ga",  &ga);
        mjson_get_long(e, el, "$.pts", &pts);

        DIYTeamStats *r = diy_ovr_row(comp, i);
        r->teamSelOffset = diy_identity_to_teamseloffset((int32_t)id);
        r->gamesPlayed  = (uint16_t)gp;
        r->wins         = (uint16_t)w;
        r->draws        = (uint16_t)d;
        r->losses       = (uint16_t)l;
        r->goalsFor     = (uint16_t)gf;
        r->goalsAgainst = (uint16_t)ga;
        r->points       = (uint16_t)pts;
        i++;
    }
}

/* Context (non-primary) type-0 league standings — the missing twin of
 * load_competition_overrides. emit_competition_overrides covers the PRIMARY
 * slot's leagueStandings typed/offset-correct, and emit_ctx_stage covers TYPE-2
 * context slots' stageSnapshot standings — but a TYPE-0 league sitting in a
 * *context* slot (secondary/tertiary/diy, e.g. a national side's FRIENDLY
 * mini-league) had NO typed standings path. Its leagueStandings rode only the
 * raw 64-bit-layout slot image, so a 32-bit BE Amiga load read it 8 bytes off
 * (the namePtr/leagueStandings 0x5C8<->0x5D0 shift): every row's teamSelOffset
 * came back 0 (all teams resolved to selectedTeams[0]) plus a stray byte in
 * `draws` (Amiga-confirmed 2026-06-17 via gdb on MILAN2.CAR's FRIENDLY — the
 * real teamSelOffsets -1/6/8/9/7/10 sat 8 bytes late). Emit the rows typed
 * (teamSelOffset->identity, 7 endian-neutral u16 stats) per context slot and
 * restore them over the raw image on load, exactly like the primary override.
 * Key "<slot>_league_rows". numTeams<=0 (empty/inactive slot) emits nothing. */
static void emit_ctx_league_standings(mjson_print_fn_t fn, void *fnd,
                                      const char *key, const DIYCompetitionEx *slot)
{
    if (slot->base.competitionType != 0 || slot->base.numTeams <= 0)
        return;
    char sk[64];
    swos_io_snprintf(sk, sizeof(sk), "%s_league_rows", key);
    mjson_printf(fn, fnd, ",\n  %Q:[", sk);
    for (int i = 0; i < DIY_OVR_LEAGUE_ROWS; i++) {
        DIYTeamStats *r = diy_ovr_row((DIYCompetitionEx *)slot, i);
        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        mjson_printf(fn, fnd,
            "{%Q:%d,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u}",
            "id",  (int)diy_teamseloffset_to_identity(r->teamSelOffset),
            "gp",  (unsigned)r->gamesPlayed,
            "w",   (unsigned)r->wins,
            "d",   (unsigned)r->draws,
            "l",   (unsigned)r->losses,
            "gf",  (unsigned)r->goalsFor,
            "ga",  (unsigned)r->goalsAgainst,
            "pts", (unsigned)r->points);
    }
    mjson_printf(fn, fnd, "%s", "]");
}

static void load_ctx_league_standings(const char *src, int src_size,
                                      const char *key, DIYCompetitionEx *slot)
{
    if (slot->base.competitionType != 0)
        return;
    char sk[64];
    swos_io_snprintf(sk, sizeof(sk), "$.%s_league_rows", key);
    const char *rows = NULL; int rows_len = 0;
    if (mjson_find(src, src_size, sk, &rows, &rows_len) != MJSON_TOK_ARRAY)
        return;                          /* pre-fix save -> raw image stands */
    int pos = 0, voff, vlen, vt, i = 0;
    while (i < DIY_OVR_LEAGUE_ROWS &&
           (pos = fast_array_next(rows, rows_len, pos, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) { i++; continue; }
        const char *e = rows + voff; int el = vlen;
        long id = 0, gp = 0, w = 0, d = 0, l = 0, gf = 0, ga = 0, pts = 0;
        mjson_get_long(e, el, "$.id",  &id);
        mjson_get_long(e, el, "$.gp",  &gp);
        mjson_get_long(e, el, "$.w",   &w);
        mjson_get_long(e, el, "$.d",   &d);
        mjson_get_long(e, el, "$.l",   &l);
        mjson_get_long(e, el, "$.gf",  &gf);
        mjson_get_long(e, el, "$.ga",  &ga);
        mjson_get_long(e, el, "$.pts", &pts);
        DIYTeamStats *r = diy_ovr_row(slot, i);
        r->teamSelOffset = diy_identity_to_teamseloffset((int32_t)id);
        r->gamesPlayed  = (uint16_t)gp;
        r->wins         = (uint16_t)w;
        r->draws        = (uint16_t)d;
        r->losses       = (uint16_t)l;
        r->goalsFor     = (uint16_t)gf;
        r->goalsAgainst = (uint16_t)ga;
        r->points       = (uint16_t)pts;
        i++;
    }
}

/* ---------- .CAR writer ---------- */

/* Forward decl — defined later (with the preset .PRE path). The career path
 * reuses it to detect whether a type-2 tournament's active stage is a league
 * (group phase) or a cup (knockout), reading the master's roundComplete. */
static int diy_stage_is_league(const DIYCompetitionEx *stage);
static int diy_stage_is_league_master(const DIYTournamentState *master);

/* VerifyTeamLoaded is extern'd further down (preset path); the career load
 * path's cup teamOffsetTable rebuild needs it earlier, so declare it here. */
extern TeamFileEx *VerifyTeamLoaded(uint16_t teamNum);

/* ---------- TYPE-2 *Context slot current-STAGE (cross-target) ----------
 *
 * The primary slot's type-2 tournament gets its active-stage image
 * (competitionFileBuffer's stageSnapshot) serialized through the typed
 * comp_slot serializer ("comp_slot_stage", in SaveJson_Write/LoadCareer), so the
 * stage's cupTeamIds@0x24E / teamGridPairings@0x2CE land at the right offset on
 * each target. The four career *Context slots (secondary/tertiary/diy/euro) did
 * NOT get this — their stageSnapshot rode only the raw competition_context
 * FLD_BYTES blob, which is byte-exact same-target but mis-positions every field
 * past the widened namePtr SDL-64 -> Amiga-32. Result: a SDL-saved euro-cup
 * (quaternary type-2) career panics in copyTitleHolderGrid on the 32-bit Amiga
 * (garbage teamNum from a mis-placed cupTeamIds; see
 * project_eurocup_context_crosstarget_gap).
 *
 * These helpers give each type-2 *Context slot the same typed stage treatment.
 * emit_comp_slot / load_comp_slot are fully slot-based (no primary hardcoding),
 * so a cup stage round-trips cupTeamIds/teamGridPairings/scores/teamOffsetTable
 * cross-target — exactly what copyTitleHolderGrid reads. League group-stage
 * standings stats are also covered per-context below (emit_ctx_stage), via
 * diy_stage_is_league_master (the master-parameterized variant of
 * diy_stage_is_league). Non-type-2 slots and pre-fix saves
 * are a no-op (the load mjson_find just misses → blob bytes stand). */
/* A type-2 tournament STAGE's cupTeamIds holds the FULL bracket — the MASTER
 * tournament's team list (master order), indexed by teamGridPairings — NOT a
 * stage-numTeams list. emit_comp_slot's `team_ids` uses the stage header's
 * numTeams (the current ROUND size, e.g. 2 for a final), so it covers only
 * cupTeamIds[0..stageNumTeams-1]. FindTeamInDIY's cup_search reads
 * cupTeamIds[teamGridPairings[d1]] where teamGridPairings are master indices
 * (e.g. 10/14), so the un-covered entries fell back to the cross-target-broken
 * blob → team not found → chairman panic 0x6006 (live-confirmed on Amiga: AC
 * Milan 0x1400 is master teamIds[14], but stage cupTeamIds[14] was blob-garbage
 * 0x2609). See project_eurocup_context_crosstarget_gap. Emit/restore the stage's
 * cupTeamIds with the MASTER team count so every bracket-referenced entry
 * round-trips. cupTeamIds is BE byte pairs (content endian-safe); the field is
 * struct-anchored so it lands at the right offset on each target. Cap below the
 * standings overlay at cupTeamIds[51]. */
static void emit_stage_cupfull(mjson_print_fn_t fn, void *fnd, const char *stageKey,
                               const DIYCompetitionEx *stage, int masterN)
{
    if (masterN < 0) masterN = 0; else if (masterN > 51) masterN = 51;
    char fk[56];
    swos_io_snprintf(fk, sizeof(fk), "%s_cupfull", stageKey);
    const uint8_t *ct = (const uint8_t *)stage->cupTeamIds;
    mjson_printf(fn, fnd, ",%Q:[", fk);
    for (int i = 0; i < masterN * 2; i++) {
        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        mjson_printf(fn, fnd, "%u", (unsigned)ct[i]);
    }
    mjson_printf(fn, fnd, "%s", "]");
}

static void load_stage_cupfull(const char *src, int src_size, const char *stageKey,
                               DIYCompetitionEx *stage, int masterN)
{
    if (masterN < 0) masterN = 0; else if (masterN > 51) masterN = 51;
    char fk[56];
    swos_io_snprintf(fk, sizeof(fk), "$.%s_cupfull", stageKey);
    const char *arr = NULL; int alen = 0;
    if (mjson_find(src, src_size, fk, &arr, &alen) == MJSON_TOK_ARRAY)
        parse_byte_array_slice(arr, alen, (uint8_t *)stage->cupTeamIds,
                               (uint32_t)masterN * 2u);
}

static void emit_ctx_stage(mjson_print_fn_t fn, void *fnd, const char *key,
                           const DIYCompetitionEx *slot)
{
    if (slot->base.competitionType != 2)
        return;
    const DIYCompetitionEx *stage = (const DIYCompetitionEx *)
        ((const uint8_t *)slot + offsetof(CompetitionSlotBuffer, stageSnapshot));
    mjson_printf(fn, fnd, "%s", ",\n  ");
    emit_comp_slot(fn, fnd, key, stage);
    emit_stage_cupfull(fn, fnd, key, stage, slot->base.numTeams);
    /* League/group active stage: standings rows. Mirrors the primary's
     * stage_standings, but PER-CONTEXT and serialising teamSelOffset (the
     * target-independent team identity: -1 careerTeam / selTeamsPtr index)
     * explicitly. The primary can rebuild it as the row index i (a national
     * tournament's teams sit in selTeamsPtr order); a euro CLUB cup's teams are a
     * SUBSET of selectedTeams so the identity must round-trip. Without this the
     * int32 teamSelOffset rode the raw stageSnapshot blob and byte-swapped on a
     * 64-bit-LE save -> 32-bit-BE Amiga load, so every group team resolved to
     * selectedTeams[0] (whole Champions-Cup group table showed the career team).
     * All values are endian-neutral decimals. */
    if (diy_stage_is_league_master((const DIYTournamentState *)slot)) {
        uint16_t nrows = slot->base.numTeams; if (nrows > 64) nrows = 64;
        char sk[64];
        swos_io_snprintf(sk, sizeof(sk), "%s_standings", key);
        mjson_printf(fn, fnd, ",\n  %Q:[", sk);
        for (uint16_t i = 0; i < nrows; i++) {
            DIYTeamStats *r = diy_ovr_row((DIYCompetitionEx *)stage, i);
            if (i > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd,
                "{%Q:%d,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u}",
                "so",  (int)r->teamSelOffset,
                "gp",  (unsigned)r->gamesPlayed,
                "w",   (unsigned)r->wins,
                "d",   (unsigned)r->draws,
                "l",   (unsigned)r->losses,
                "gf",  (unsigned)r->goalsFor,
                "ga",  (unsigned)r->goalsAgainst,
                "pts", (unsigned)r->points);
        }
        mjson_printf(fn, fnd, "%s", "]");
    }
}

static void load_ctx_stage(const char *src, int src_size, const char *key,
                           DIYCompetitionEx *slot)
{
    if (slot->base.competitionType != 2)
        return;
    DIYCompetitionEx *stage = (DIYCompetitionEx *)
        ((uint8_t *)slot + offsetof(CompetitionSlotBuffer, stageSnapshot));
    load_comp_slot(src, src_size, key, stage);
    load_stage_cupfull(src, src_size, key, stage, slot->base.numTeams);
    /* League/group stage standings restore (see emit_ctx_stage): teamSelOffset
     * (identity) + uint16 stats from typed decimals, overwriting the
     * byte-swapped raw-blob bytes. Absent in pre-fix saves -> mjson_find misses
     * -> no-op (blob bytes stand). */
    if (diy_stage_is_league_master((const DIYTournamentState *)slot)) {
        char sk[64];
        swos_io_snprintf(sk, sizeof(sk), "$.%s_standings", key);
        const char *rows = NULL; int rows_len = 0;
        if (mjson_find(src, src_size, sk, &rows, &rows_len) == MJSON_TOK_ARRAY) {
            uint16_t nrows = slot->base.numTeams; if (nrows > 64) nrows = 64;
            int pos = 0, voff, vlen, vt; uint16_t i = 0;
            while (i < nrows &&
                   (pos = fast_array_next(rows, rows_len, pos,
                                          &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                const char *e = rows + voff; int el = vlen;
                long so = 0, gp = 0, w = 0, d = 0, l = 0, gf = 0, ga = 0, pts = 0;
                mjson_get_long(e, el, "$.so",  &so);
                mjson_get_long(e, el, "$.gp",  &gp);
                mjson_get_long(e, el, "$.w",   &w);
                mjson_get_long(e, el, "$.d",   &d);
                mjson_get_long(e, el, "$.l",   &l);
                mjson_get_long(e, el, "$.gf",  &gf);
                mjson_get_long(e, el, "$.ga",  &ga);
                mjson_get_long(e, el, "$.pts", &pts);
                DIYTeamStats *r = diy_ovr_row(stage, i);
                r->teamSelOffset = (int32_t)so;
                r->gamesPlayed  = (uint16_t)gp;
                r->wins         = (uint16_t)w;
                r->draws        = (uint16_t)d;
                r->losses       = (uint16_t)l;
                r->goalsFor     = (uint16_t)gf;
                r->goalsAgainst = (uint16_t)ga;
                r->points       = (uint16_t)pts;
                i++;
            }
        }
    }
}

int16_t SaveJson_WriteCareer(uint8_t *filename)
{
    JsonOutBuf out = { NULL, 0, 0, 0 };

    /* Reset string pool — emit_str_pool_field consumers (the four
     * seasonInformations comp-name slots) read pointer-typed fields
     * whose post-load value lives in this pool. Resetting up front
     * mirrors the load side so the same content interns at the same
     * pool offset, keeping save → load → save byte-
     * stable round-trip with the loader. */
    string_pool_reset();

    /* Top-level object. Every saved field is a typed semantic field
     * (verified at runtime with SWOS_SAVE_FORMAT_JSON_VERIFY — see
     * verify_roundtrip_fields). Save files are pointer-portable and
     * 64-bit-friendly by construction. */
    mjson_printf(json_out_emit, &out,
        "{\n"
        "  %Q: %d,\n"
        "  %Q: %Q,\n"
        "  %Q: %Q,\n"
        "  %Q: %d,\n  ",
        "schema_version", SAVE_JSON_SCHEMA_VERSION,
        "file_kind",      KIND_CAREER,
        "world",          World_Tag(),
        "num_selected_teams", (int)_numSelectedTeams);

    emit_season_informations(json_out_emit, &out);
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    /* careerCompetitionSlotPtrTable / *CompetitionPtr / *CompStringOffset /
     * careerCupPickList are all re-derivable from non-pointer state on
     * load — RebuildPointersAfterLoad() handles them. Not saved.
     *
     * EXCEPT: when selTeamNumber is the 0xFFFF "no team" sentinel
     * (season mode, all-CPU teams), the rebuild can't recover the
     * country/league index from selTeamNumber alone. Capture it from
     * the live currentCompetitionPtr->teamNumber (Competition struct
     * offset 18) so the rebuild can look up competitionsTable[] on
     * load. 0xFF when no comp is active (career not yet started). */
    {
        uint8_t pcc = 0xFF;
        if (_currentCompetitionPtr) {
            pcc = (uint8_t)((Competition *)_currentCompetitionPtr)->teamNumber;
        }
        mjson_printf(json_out_emit, &out,
                     "%Q:%d,\n  ",
                     "primary_country_code", (int)pcc);
    }
    /* *Copy struct text[2] pointers are template-static — see
     * restore_comp_copy_text_ptrs_from_template — so they're NOT
     * encoded in the save. They get restored from each careerContests
     * src template on load. */
    /* The five competition slots' raw blobs carry the header's namePtr, an absolute
     * address that RebuildPointersAfterLoad re-derives on load: written as it was, it
     * made two saves of the same career differ run to run (address randomisation) --
     * found by the golden career test. Written as 1 when set and 0 when not (the
     * rebuild re-points only a non-NULL one: it marks a slot in use), and put back
     * straight after. */
    {
        CompetitionSlotBuffer *slots[5] = {
            &_primaryCompetitionBuf, &_competitionContextBuf, &_tertiaryCompetitionContextBuf,
            &_euroCupsCompetitionContextBuf, &_diyCompetitionContextBuf };
        uint8_t *names[5], *snapNames[5];
        uint8_t *const one = (uint8_t *)(uintptr_t)1;
        for (int i = 0; i < 5; i++) {
            /* ...and the stage snapshot's own copy of the header (a byte image). */
            uint8_t *snap = slots[i]->stageSnapshot + offsetof(DIYCompetition, namePtr);
            names[i] = slots[i]->tourn.header.namePtr;
            slots[i]->tourn.header.namePtr = names[i] ? one : NULL;
            memcpy(&snapNames[i], snap, sizeof snapNames[i]);
            memcpy(snap, snapNames[i] ? &one : &snapNames[i], sizeof snapNames[i]);
        }
        emit_scalar_table(json_out_emit, &out, "career_scalars", s_career_scalars);
        for (int i = 0; i < 5; i++) {
            slots[i]->tourn.header.namePtr = names[i];
            memcpy(slots[i]->stageSnapshot + offsetof(DIYCompetition, namePtr), &snapNames[i],
                   sizeof snapNames[i]);
        }
    }

    /* Cross-target competition-buffer override (league standings) — see
     * emit_competition_overrides. Re-emits the layout/endian-sensitive
     * fields the raw blobs can't carry across a 32/64-bit + LE/BE boundary.
     * Sibling key of career_scalars. */
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_competition_overrides(json_out_emit, &out);

    /* The 34 *Copy competition records' STATIC parts are reconstructed from
     * their templates on load (not saved as a raw blob — see
     * reconstruct_comp_copy_region_from_templates). The 2 dynamic descriptor
     * bytes (comp_copy_dynamic) AND the per-career team-list regions
     * (comp_copy_teamlists) are carried here so a loaded career's cup brackets
     * survive instead of reverting to template teams (the euro-cup 0x3443 dup). */
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_comp_copy_dynamic(json_out_emit, &out);
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_comp_copy_teamlists(json_out_emit, &out);

    /* Unified typed competition slots — emit_comp_slot for all 5 slots
     * (primary + 4 contexts) via their DIYCompetitionEx struct view, so
     * the cross-target-sensitive header + team-id fields round-trip across
     * the 32/64-bit + LE/BE boundary. Kept in PARALLEL with the raw blobs
     * during transition (VERIFY proves identical same-target bytes); the
     * blobs + primary_comp_* runs + the league_rows override get retired
     * once all slots are proven. */
    mjson_printf(json_out_emit, &out, ",\n  %Q:{", "comp_slots");
    mjson_printf(json_out_emit, &out, "%Q:%d", "comp_slots_present", 1);
    mjson_printf(json_out_emit, &out, "%s", ",");
    emit_comp_slot(json_out_emit, &out, "primary",
                   (const DIYCompetitionEx *)&_primaryCompetitionBuf);
    mjson_printf(json_out_emit, &out, "%s", ",");
    emit_comp_slot(json_out_emit, &out, "secondary",
                   (const DIYCompetitionEx *)&_competitionContextBuf);
    mjson_printf(json_out_emit, &out, "%s", ",");
    emit_comp_slot(json_out_emit, &out, "tertiary",
                   (const DIYCompetitionEx *)&_tertiaryCompetitionContextBuf);
    mjson_printf(json_out_emit, &out, "%s", ",");
    emit_comp_slot(json_out_emit, &out, "diy",
                   (const DIYCompetitionEx *)&_diyCompetitionContextBuf);
    mjson_printf(json_out_emit, &out, "%s", ",");
    emit_comp_slot(json_out_emit, &out, "euro",
                   (const DIYCompetitionEx *)&_euroCupsCompetitionContextBuf);
    mjson_printf(json_out_emit, &out, "%s", "}");

    /* TYPE-2 tournament current-STAGE (cross-target) — mirrors the preset
     * write_competition_buf path. When the primary slot is a tournament
     * (national-team WORLD CUP / EUROPEAN CHAMPIONSHIPS), the active stage
     * (group league / knockout cup) lives at competitionFileBuffer's
     * stageSnapshot (Phase B; was the legacy +0x420 overlay).
     * The master is serialized typed by comp_slots above, but the stage rides
     * the raw blob and byte-swaps on a 64-bit->Amiga load. Emit it typed +
     * endian-neutral: comp_slot_stage (struct-anchored header + per-type
     * fields) + the group standings stats (league) or the three knockout
     * score arrays (cup). On load these are re-laid and the cup teamOffsetTable
     * is rebuilt careerTeam-aware via VerifyTeamLoaded (SaveJson_LoadCareer),
     * so a managed national team (careerTeam, negative selTeamsPtr offset)
     * resolves correctly cross-target. */
    if (((const DIYCompetitionEx *)&_primaryCompetitionBuf)
            ->base.competitionType == 2) {
        const DIYCompetitionEx *stage = (const DIYCompetitionEx *)
            ((const uint8_t *)&_primaryCompetitionBuf + offsetof(CompetitionSlotBuffer, stageSnapshot));
        mjson_printf(json_out_emit, &out, ",\n  ");
        emit_comp_slot(json_out_emit, &out, "comp_slot_stage", stage);
        emit_stage_cupfull(json_out_emit, &out, "comp_slot_stage", stage,
                           ((const DIYCompetitionEx *)&_primaryCompetitionBuf)->base.numTeams);
        if (diy_stage_is_league(stage)) {
            uint16_t nrows = _primaryCompetitionBuf.tourn.header.numTeams; if (nrows > 64) nrows = 64;
            mjson_printf(json_out_emit, &out, ",\n  %Q:[", "stage_standings");
            for (uint16_t i = 0; i < nrows; i++) {
                DIYTeamStats *r = diy_ovr_row((DIYCompetitionEx *)stage, i);
                if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
                mjson_printf(json_out_emit, &out,
                    "{%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u}",
                    "gp",  (unsigned)r->gamesPlayed,
                    "w",   (unsigned)r->wins,
                    "d",   (unsigned)r->draws,
                    "l",   (unsigned)r->losses,
                    "gf",  (unsigned)r->goalsFor,
                    "ga",  (unsigned)r->goalsAgainst,
                    "pts", (unsigned)r->points);
            }
            mjson_printf(json_out_emit, &out, "%s", "]");
        } else {
            const uint16_t *st = (const uint16_t *)&stage->matchData.cup.cupRoundResults[0x40];
            const uint16_t *ag = stage->cupAggregateScores;
            const uint16_t *pn = (const uint16_t *)&stage->leagueSectionState.phaseStartMonths[0];
            const char *keys[3] = { "stage_score_tab", "stage_score_agg", "stage_score_pen" };
            const uint16_t *arrs[3] = { st, ag, pn };
            for (int a = 0; a < 3; a++) {
                mjson_printf(json_out_emit, &out, ",\n  %Q:[", keys[a]);
                for (int i = 0; i < 18; i++) {
                    if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
                    mjson_printf(json_out_emit, &out, "%u", (unsigned)arrs[a][i]);
                }
                mjson_printf(json_out_emit, &out, "%s", "]");
            }
        }
    }

    /* TYPE-2 *Context slots' current stage — cross-target fix (see emit_ctx_stage).
     * Each emits "<slot>_comp_slot_stage" only if that slot is a type-2
     * tournament; no-op otherwise (no dangling comma). */
    emit_ctx_stage(json_out_emit, &out, "secondary_comp_slot_stage",
                   (const DIYCompetitionEx *)&_competitionContextBuf);
    emit_ctx_stage(json_out_emit, &out, "tertiary_comp_slot_stage",
                   (const DIYCompetitionEx *)&_tertiaryCompetitionContextBuf);
    emit_ctx_stage(json_out_emit, &out, "diy_comp_slot_stage",
                   (const DIYCompetitionEx *)&_diyCompetitionContextBuf);
    emit_ctx_stage(json_out_emit, &out, "euro_comp_slot_stage",
                   (const DIYCompetitionEx *)&_euroCupsCompetitionContextBuf);

    /* TYPE-0 *Context slots' league standings — cross-target fix (see
     * emit_ctx_league_standings). Emits "<slot>_league_rows" only for a type-0
     * context league with teams (e.g. a national FRIENDLY); no-op otherwise. */
    emit_ctx_league_standings(json_out_emit, &out, "secondary",
                   (const DIYCompetitionEx *)&_competitionContextBuf);
    emit_ctx_league_standings(json_out_emit, &out, "tertiary",
                   (const DIYCompetitionEx *)&_tertiaryCompetitionContextBuf);
    emit_ctx_league_standings(json_out_emit, &out, "diy",
                   (const DIYCompetitionEx *)&_diyCompetitionContextBuf);
    emit_ctx_league_standings(json_out_emit, &out, "euro",
                   (const DIYCompetitionEx *)&_euroCupsCompetitionContextBuf);

    /* teamSelectionKeyCache stride list (endian-neutral) — paired with the
     * raw cache blob; re-lays the host-native +2 strides on load. */
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_cache_strides(json_out_emit, &out);

    /* Two top-level TeamFileEx records (careerTeam, currentTeam) and
     * the variable-length selTeamsPtr array following
     * numSelectedTeams in the save. */
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_team_file_ex(json_out_emit, &out, "career_team",  &_careerTeam);
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_team_file_ex(json_out_emit, &out, "current_team", &_currentTeam);

    mjson_printf(json_out_emit, &out, ",\n  %Q:[", "selected_teams");
    for (uint16_t i = 0; i < _numSelectedTeams; i++) {
        if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
        emit_team_file_ex_body(json_out_emit, &out,
                               (uint8_t *)(_selectedTeams + i));
    }
    mjson_printf(json_out_emit, &out, "%s", "]");

    /* TMD team pool — emit only the populated count (= tmd_team_count
     * scalar = uint16 at &careerFileBuffer + 0). Empty trailing slots
     * are reconstructed by careerFileBufferBuf memset on load. */
    {
        uint16_t tmd_count = *(uint16_t *)&_careerFileBuffer;
        mjson_printf(json_out_emit, &out, ",\n  %Q:[", "tmd_teams");
        for (uint16_t i = 0; i < tmd_count; i++) {
            if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
            emit_team_file_ex_body(json_out_emit, &out,
                                   (uint8_t *)((TeamFileEx *)_tmdFileBuffer + i));
        }
        mjson_printf(json_out_emit, &out, "%s", "]");
    }

    /* A world's pools below the leagues (team_pool.c): per pooled country, which club each
     * swapped slot of its team file is now, and the queue below -- identities by number (a
     * slot of the file, 256 + a club of the pool), never records. */
    int pools = 0;
    for (int k = 0; k < TEAM_POOL_FILES; k++) {
        const TeamPoolState *st = &g_teamPool[k];
        if (!st->init)
            continue;
        if (!pools++)
            mjson_printf(json_out_emit, &out, ",\n  %Q:[", "team_pools");
        else
            mjson_printf(json_out_emit, &out, "%s", ",");
        mjson_printf(json_out_emit, &out, "{%Q:%d,%Q:[", "country", (int)st->country, "queue");
        for (int i = 0; i < st->queued; i++)
            mjson_printf(json_out_emit, &out, "%s%d", i ? "," : "", (int)st->queue[i]);
        mjson_printf(json_out_emit, &out, "],%Q:[", "map");
        for (int i = 0; i < TEAM_POOL_SLOTS; i++)
            mjson_printf(json_out_emit, &out, "%s%d", i ? "," : "", (int)st->map[i]);
        mjson_printf(json_out_emit, &out, "%s", "]}");
    }
    if (pools)
        mjson_printf(json_out_emit, &out, "%s", "]");

    /* A world's cups (world_cups.c): the seasons the career has ended in the world, the
     * league cup's winner and its season. */
    if (World_Current() != WORLD_9697) {
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:[", "world_cups",
                     "seasons", (int)g_worldCups.seasons,
                     "league_cup_winner", (int)g_worldCups.leagueCupWinner,
                     "league_cup_season", (int)g_worldCups.leagueCupSeason,
                     "places_season", (int)g_worldCups.placesSeason,
                     "places_country", (int)g_worldCups.placesCountry, "places");
        for (int i = 0; i < 256; i++)
            mjson_printf(json_out_emit, &out, "%s%d", i ? "," : "", (int)g_worldCups.place[i]);
        mjson_printf(json_out_emit, &out, "%s", "]}");
    }

    /* A world's Coppa Italia Serie D (world_cupd.c): the season, the round to come, the
     * drawn round's pairs, each tie's winner and host. */
    if (World_CupD()) {
        const WorldCupDState *d = &g_worldCupD;
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%d,%Q:%d,%Q:[%d,%d,%d,%d],%Q:[", "world_cupd",
                     "season", (int)d->season, "round", (int)d->round, "drawn", (int)d->drawn,
                     "drawn_ties", (int)d->sd[0], (int)d->sd[1], (int)d->sd[2], (int)d->sd[3], "results");
        for (int i = 0; i < WORLD_CUPD_TIES; i++)
            mjson_printf(json_out_emit, &out, "%s%d", i ? "," : "", (int)d->res[i]);
        mjson_printf(json_out_emit, &out, "],%Q:[", "hosts");
        for (int i = 0; i < WORLD_CUPD_TIES; i++)
            mjson_printf(json_out_emit, &out, "%s%d", i ? "," : "", (int)d->home[i]);
        mjson_printf(json_out_emit, &out, "],%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:[",
                     "built", (int)d->built, "tie", (int)d->bt, "week1", (int)d->d1, "week2", (int)d->d2,
                     "legs", (int)d->legs, "team_a", (int)d->teamA, "team_b", (int)d->teamB,
                     "generated", (int)d->gen, "preliminary_ties", (int)d->np, "sides");
        for (int i = 0; i < 2 * (WORLD_CUPD_PRE + WORLD_CUPD_R1); i++)
            mjson_printf(json_out_emit, &out, "%s%d", i ? "," : "", (int)d->side[i]);
        mjson_printf(json_out_emit, &out, "],%Q:%d}", "slot2", (int)d->slot2);
    }

    /* A world's play-off the career club plays (world_playoff.c): which, its clubs (team
     * numbers), their places, points and the round under way, the other groups' tables
     * simulated early, the winner once decided. */
    if (g_worldPlayoff.kind) {
        const WorldPlayoffState *w = &g_worldPlayoff;
        uint16_t rows[2 * WORLD_PLAYOFF_EARLY_ROWS * 8];
        int nr = 0;
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d", "world_playoff",
                     "kind", (int)w->kind, "etp", (int)w->etp, "rules", (int)w->rules,
                     "winner", (int)w->winner, "cup", (int)w->cup, "ties", (int)w->ties,
                     "early", (int)w->early);
        mjson_printf(json_out_emit, &out, ",%Q:%d", "final_rules", (int)w->rules2);
        wp_emit_u16(&out, "teams", w->team, w->n);
        wp_emit_u8(&out, "places", w->prio, w->n);
        wp_emit_u16(&out, "points", w->pts, w->n);
        wp_emit_u16(&out, "won", w->won, w->n);
        wp_emit_u16(&out, "scored", w->gf, w->n);
        wp_emit_u8(&out, "table_places", w->place, w->n);
        wp_emit_u8(&out, "groups", w->group, w->n);
        wp_emit_u8(&out, "tie_a", w->tieA, w->ties);
        wp_emit_u8(&out, "tie_b", w->tieB, w->ties);
        wp_emit_u8(&out, "early_divisions", w->earlyDiv, w->early);
        wp_emit_u8(&out, "early_counts", w->earlyN, w->early);
        for (int e = 0; e < w->early && e < 2; e++)     /* (team, played, won, drawn, lost, gf, ga, pts) */
            for (int k = 0; k < w->earlyN[e] && k < WORLD_PLAYOFF_EARLY_ROWS; k++) {
                const WorldPlayoffRow *r = &w->earlyRow[e][k];
                rows[nr++] = r->team; rows[nr++] = r->played; rows[nr++] = r->won; rows[nr++] = r->drawn;
                rows[nr++] = r->lost; rows[nr++] = r->gf; rows[nr++] = r->ga; rows[nr++] = r->pts;
            }
        wp_emit_u16(&out, "early_rows", rows, nr);
        if (w->kind == WORLD_PLAYOFF_SPEC) {    /* (a play-off of the pack's spec: which, its ties) */
            const uint8_t id[5] = { w->country, w->pidx, w->stage, w->season, w->week };
            wp_emit_u8(&out, "spec", id, 5);
            wp_emit_u8(&out, "tie_winners", w->tw, WORLD_PLAYOFF_MAX);
            wp_emit_u8(&out, "tie_losers", w->tl, WORLD_PLAYOFF_MAX);
            wp_emit_u8(&out, "round_ties", w->tieT, 16);
            wp_emit_u16(&out, "global_numbers", w->glob, w->n);
        }
        mjson_printf(json_out_emit, &out, "%s", "}");
    }

    /* A world's stage titles decided mid-season (world_playoff.c): per stage the season,
     * whether decided, the champion's and the runner-up's team and global numbers. */
    {
        int any = 0;
        uint16_t st[3 * 6];
        for (int k = 0; k < 3; k++) {
            const WorldStageTitle *x = &g_worldStageTitle[k];
            any |= x->done;
            st[6 * k] = x->season; st[6 * k + 1] = x->done;
            st[6 * k + 2] = x->team[0]; st[6 * k + 3] = x->team[1];
            st[6 * k + 4] = x->glob[0]; st[6 * k + 5] = x->glob[1];
        }
        if (any) {
            mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:0", "stage_titles", "version");
            wp_emit_u16(&out, "stages", st, 3 * 6);
            mjson_printf(json_out_emit, &out, "%s", "}");
        }
    }

    /* A world's UEFA coefficients (uefa_coef.c): per association its clubs and points this
     * season and its 5 ring seasons (thousandths), per club (team number) its points this season
     * and its ring's, each cup's last knockout round and final. */
    if (g_uefaCoef.init) {
        const UefaCoefState *u = &g_uefaCoef;
        static uint16_t kc[UEFA_COEF_CLUBS * 7];
        uint16_t prev[3 * 16], fin[6];
        int nk = 0;
        for (int i = 0; i < u->nClubs; i++) {
            kc[nk++] = u->club[i].team;
            kc[nk++] = u->club[i].pts;
            for (int k = 0; k < 5; k++)
                kc[nk++] = u->club[i].hist[k];
        }
        for (int c = 0; c < 3; c++) {
            for (int i = 0; i < 16; i++)
                prev[c * 16 + i] = u->prev[c][i];
            fin[2 * c] = u->fin[c][0];
            fin[2 * c + 1] = u->fin[c][1];
        }
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%d", "uefa_coef", "ring", (int)u->ring,
                     "season_closed", (int)u->done);
        wp_emit_u8(&out, "association_clubs", u->teams, 256);
        wp_emit_u32(&out, "association_points", u->pts, 256);
        wp_emit_u32(&out, "association_seasons", &u->hist[0][0], 5 * 256);
        wp_emit_u16(&out, "clubs", kc, nk);
        wp_emit_u8(&out, "previous_counts", u->prevN, 3);
        wp_emit_u16(&out, "previous", prev, 3 * 16);
        wp_emit_u16(&out, "finals", fin, 6);
        wp_emit_u8(&out, "phase_ended", u->phaseEnded, 3);
        mjson_printf(json_out_emit, &out, "%s", "}");
    }

    /* A world's qualifying rounds (uefa_access.c's state): the season's bracket, kept for its
     * points at the next season end -- per tie its round, whether its first club went through,
     * the two clubs (the Mod's team words, file | ordinal << 8). */
    if (g_uefaCoef.init && (g_uqState.nbr || g_uqState.mark)) {
        const struct uq_state *q = &g_uqState;
        static uint16_t br[NBR * 4];
        uint16_t gl[3 * 36];
        int nb = 0;
        for (int i = 0; i < q->nbr && i < NBR; i++) {
            br[nb++] = q->br[i].r;
            br[nb++] = q->br[i].aw;
            br[nb++] = q->br[i].a;
            br[nb++] = q->br[i].b;
        }
        for (int c = 0; c < 3; c++)
            for (int i = 0; i < 36; i++)
                gl[c * 36 + i] = q->gl[c][i];
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d", "uefa_qualifying",
                     "mark", (int)q->mark, "manager_club", (int)q->m, "stand_in", (int)q->s,
                     "stand_in_cup", (int)q->sc, "ties_played", (int)q->k, "results", (int)q->res);
        wp_emit_u8(&out, "list_counts", q->gn, 3);
        wp_emit_u16(&out, "lists", gl, 3 * 36);
        wp_emit_u16(&out, "bracket", br, nb);
        mjson_printf(json_out_emit, &out, "%s", "}");
    }

    /* A world's stage tables (league_playoffs.c): per league (its country) the tables kept at
     * its fixture stages' ends, for the season end's stage-title and title play-offs -- rows
     * as (team, global number, won, for, against, points) -- and a 'keep' stage's table. */
    {
        int nb = 0;
        static uint16_t rows[LEAGUE_STAGE_TABLES * 32 * 6];
        for (int i = 0; i < LEAGUE_STAGE_BANKS; i++) {
            const LeagueStageBank *b = &g_leagueStages[i];
            if (!b->used)
                continue;
            if (nb++)
                mjson_printf(json_out_emit, &out, "%s", ",");
            else
                mjson_printf(json_out_emit, &out, ",\n  %Q:[", "league_stages");
            mjson_printf(json_out_emit, &out, "{%Q:%d,%Q:%d,%Q:%d", "country", (int)b->key, "fresh", (int)b->fresh,
                         "kept_division", (int)b->keptDiv);
            wp_emit_u8(&out, "clubs", b->n, LEAGUE_STAGE_TABLES);
            int nr = 0;
            for (int k = 0; k < LEAGUE_STAGE_TABLES; k++)
                for (int r = 0; r < b->n[k] && r < 32; r++) {
                    const LeagueStageRow *x = &b->r[k][r];
                    rows[nr++] = x->team; rows[nr++] = x->glob; rows[nr++] = x->won;
                    rows[nr++] = x->gf; rows[nr++] = x->ga; rows[nr++] = x->pts;
                }
            wp_emit_u16(&out, "tables", rows, nr);
            nr = 0;
            for (int r = 0; r < b->nKept && r < 32; r++) {
                const LeagueStageRow *x = &b->kept[r];
                rows[nr++] = x->team; rows[nr++] = x->glob; rows[nr++] = x->won;
                rows[nr++] = x->gf; rows[nr++] = x->ga; rows[nr++] = x->pts;
            }
            wp_emit_u16(&out, "kept", rows, nr);
            mjson_printf(json_out_emit, &out, "%s", "}");
        }
        if (nb)
            mjson_printf(json_out_emit, &out, "%s", "]");
    }

    /* A world's other confederations' cups (world_conti.c): each cup's holder (team word as the
     * Mod's, association, strength, ranking points) and the edition under way -- its entrants
     * (team word, cup << 4 | round, strength / 16 | slot << 12), the career club's results. */
#if SWOS_WORLDS
    if (CT_ST.mark == 0x5443 || CT_ED.mark == 0x4543) {
        static uint32_t hold[CT_NCUP * 4];
        static uint16_t ent[CT_NED * 3];
        uint16_t res[16 * 3];
        for (int c = 0; c < CT_NCUP; c++) {
            hold[4 * c] = CT_ST.hold[c].id; hold[4 * c + 1] = CT_ST.hold[c].assoc;
            hold[4 * c + 2] = CT_ST.hold[c].str; hold[4 * c + 3] = CT_ST.hold[c].pts;
        }
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%d", "conti_holders", "mark", (int)CT_ST.mark,
                     "season", (int)CT_ST.season);
        wp_emit_u32(&out, "holders", hold, CT_NCUP * 4);
        mjson_printf(json_out_emit, &out, "%s", "}");
        int ne = 0;
        for (int i = 0; i < CT_ED.ne && i < CT_NED; i++) {
            ent[ne++] = (uint16_t)(CT_ED.e[i].id0 | CT_ED.e[i].id1 << 8);
            ent[ne++] = CT_ED.e[i].cr;
            ent[ne++] = (uint16_t)(CT_ED.e[i].s0 | CT_ED.e[i].s1 << 8);
        }
        for (int i = 0; i < 16; i++) {
            res[3 * i] = CT_ED.f[i].c; res[3 * i + 1] = CT_ED.f[i].r; res[3 * i + 2] = CT_ED.f[i].v;
        }
        mjson_printf(json_out_emit, &out, ",\n  %Q:{%Q:%d,%Q:%lu,%Q:%d,%Q:%d", "conti_edition",
                     "mark", (int)CT_ED.mark, "seed", (unsigned long)CT_ED.seed, "manager_club", (int)CT_ED.mgr,
                     "results_count", (int)CT_ED.nf);
        wp_emit_u16(&out, "entrants", ent, ne);
        wp_emit_u16(&out, "results", res, 16 * 3);
        mjson_printf(json_out_emit, &out, "%s", "}");
    }
#endif

    /* Foreign-market player pool — fixed-size MarketPlayer[432]. Emit
     * all 432 slots so unpopulated entries (teamId == 0) round-trip
     * faithfully; the array is dense in practice for active careers. */
    {
        mjson_printf(json_out_emit, &out, ",\n  %Q:[", "career_foreign_market_players");
        for (uint32_t i = 0; i < 432; i++) {
            if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
            emit_market_player_body(json_out_emit, &out,
                                    _careerForeignMarketPlayers + i * sizeof(MarketPlayer));
        }
        mjson_printf(json_out_emit, &out, "%s", "]");
    }

    /* Transfer-offer + job-offer tables — fixed-size arrays of typed objects
     * (cross-target-safe; replaces the host-native FLD_BYTES blobs). */
    {
        mjson_printf(json_out_emit, &out, ",\n  %Q:[", "transfer_offer_table");
        for (uint32_t i = 0; i < 5; i++) {
            if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
            emit_struct_object(json_out_emit, &out,
                               _transferOfferTable + i * sizeof(TransferOfferEntry),
                               s_transfer_offer_fields);
        }
        mjson_printf(json_out_emit, &out, "%s", "]");

        const struct { const char *key; const uint8_t *base; } jobTabs[2] = {
            { "new_job_offers", _newJobOffers }, { "job_offers", _jobOffers },
        };
        for (int t = 0; t < 2; t++) {
            mjson_printf(json_out_emit, &out, ",\n  %Q:[", jobTabs[t].key);
            for (uint32_t i = 0; i < 5; i++) {
                if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
                emit_struct_object(json_out_emit, &out,
                                   jobTabs[t].base + i * sizeof(JobOffer),
                                   s_job_offer_fields);
            }
            mjson_printf(json_out_emit, &out, "%s", "]");
        }
    }

    /* Career / current team reserve players — 16 × PlayerFile each,
     * extending the 16-player roster in career_team / current_team
     * to the on-disk 32-player slot. */
    {
        const char *keys[2] = { "career_team_reserves", "current_team_reserves" };
        const uint8_t *bases[2] = { (uint8_t *)&_careerTeam  + 684,
                                    (uint8_t *)&_currentTeam + 684 };
        for (int t = 0; t < 2; t++) {
            mjson_printf(json_out_emit, &out, ",\n  %Q:[", keys[t]);
            for (int i = 0; i < 16; i++) {
                if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
                emit_player_file_body(json_out_emit, &out,
                                      bases[t] + (size_t)i * sizeof(PlayerFile));
            }
            mjson_printf(json_out_emit, &out, "%s", "]");
        }
    }

    mjson_printf(json_out_emit, &out, "%s", "\n}\n");

    if (out.oom || !out.ptr) {
        if (out.ptr) swos_io_free(out.ptr);
        return 1;
    }

    int16_t result = WriteFile(filename, (uint8_t *)out.ptr, (uint32_t)out.len);
    swos_io_free(out.ptr);
    if (result != 0) {
        ShowFsErrorByCode((uint16_t)result);
        return 1;
    }

#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
    /* Per-FIELD verify over s_career_scalars (see verify_roundtrip_fields): diffs
     * exactly the bytes the save captured, across every standalone extracted
     * struct. Replaces the old single-region [&careerFileBuffer, +data_size)
     * diff — that region is the careerFileBufferBuf overlay, which post-untangle
     * holds only dead .skip zeros + 2 scalars, so it verified almost nothing.
     * The seasonInformations comp-name pointer slots that once needed exempting
     * now live outside this set entirely (career_season_info.c).
     * Not for the golden career test's season saves (src/career_autopilot.c): the
     * verify leaves the career RELOADED, and the test records a career nobody saves. */
#ifdef SWOS_SDL_BUILD
    { extern int32_t CareerAutopilot_Active(void);
      if (!CareerAutopilot_Active())
          verify_roundtrip_fields("career", s_career_scalars,
                                  verify_reload_career, filename); }
#else
    verify_roundtrip_fields("career", s_career_scalars,
                            verify_reload_career, filename);
#endif
#endif

    return 0;
}

/* Probe the first bytes of the named save file. Used by the
 * file_io.c dispatcher to decide which loader to invoke without
 * having to stage the whole file through LoadFileEx (which the
 * JSON-format size would overflow). Returns 1 if the file begins
 * with `{` (after whitespace), 0 otherwise, or -1 on open error.
 *
 * Implemented via swos_io_load_alloc — wasteful in that we load the
 * whole file just to peek at the head, but save files are small and
 * the resulting buffer is then dropped. Keeps the I/O path uniform
 * across SDL and Amiga. */
int SaveJson_FileIsJson(const uint8_t *filename)
{
    uint8_t *buf = (uint8_t *)0;
    uint32_t size = 0;
    if (swos_io_load_alloc(filename, &buf, &size) != 0) return -1;
    int verdict = 0;
    uint32_t probe = (size < 16u) ? size : 16u;
    for (uint32_t i = 0; i < probe; i++) {
        uint8_t c = buf[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        verdict = (c == '{') ? 1 : 0;
        break;
    }
    swos_io_free(buf);
    return verdict;
}

/* One pool's state (team_pool.h) from its save object. */
static void load_team_pool(const char *tp, int tplen, TeamPoolState *st)
{
    long v = 0;
    const char *arr;
    int al, pos = 0, voff, vlen, vt, i = 0;
    if (mjson_get_long(tp, tplen, "$.country", &v) > 0)
        st->country = (uint8_t)v;
    if (mjson_find(tp, tplen, "$.queue", &arr, &al) == MJSON_TOK_ARRAY)
        while (i < TEAM_POOL_QUEUE && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
            if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                st->queue[i++] = (uint16_t)v;
    st->queued = (uint8_t)i;
    pos = 0;
    i = 0;
    if (mjson_find(tp, tplen, "$.map", &arr, &al) == MJSON_TOK_ARRAY)
        while (i < TEAM_POOL_SLOTS && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
            if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                st->map[i++] = (uint16_t)v;
    st->init = 1;
}

/* ---------- .CAR loader ---------- */

/* Helper that runs the named-blob + semantic field loaders in
 * dependency order. Used by both the main load path (after
 * opaque_data has been laid down) and the verify path (against a
 * zeroed buffer so we can diff). */
static void apply_semantic_load(const char *src, int src_size)
{
    /* Reset the string pool up front so any { str: "..." } pointer
     * fields decoded during this load start interning at pool[0].
     * Without this, repeated loads (e.g. the verify pass) intern at
     * different pool offsets and pointer fields show up as
     * byte-level mismatches. */
    string_pool_reset();

    PROF_START(season_info);
    load_season_informations(src, src_size);
    PROF_END(season_info, "career-load");
    /* Top-level pointer fields were removed from the save format —
     * RebuildPointersAfterLoad() re-derives them after this returns.
     * For the rebuild's selTeamNumber==0xFFFF (season-mode) path, parse
     * the saved primary-comp country byte; default to 0xFF if missing
     * (legacy save). Stored to a module-local; career_init.c reads it
     * via SaveJson_GetLoadedPrimaryCountryCode(). */
    {
        long v = 0xFF;
        if (mjson_get_long(src, src_size, "$.primary_country_code", &v) > 0) {
            s_loaded_primary_country_code = (uint8_t)v;
        } else {
            s_loaded_primary_country_code = 0xFF;
        }
    }
    /* *Copy competition records: reconstruct each from its static template
     * (host-correct sizes incl. text[2] pointer width) + re-apply the 2
     * dynamic bytes. Replaces the old raw comp_copy_region blob, which
     * embedded width-dependent pointers and broke cross-target (32-bit
     * read the embedded team-id list 8 bytes early → infinite team-load
     * loop). See load_comp_copy_region. Full record copy from template
     * subsumes the old restore_comp_copy_text_ptrs_from_template pointer
     * fixup. */
    load_comp_copy_region(src, src_size);
    PROF_START(scalars);
    load_scalar_table(src, src_size, "career_scalars", s_career_scalars);
    PROF_END(scalars, "career-load");

    /* Re-lay teamSelectionKeyCache strides host-native AFTER the cache blob
     * + its count loaded (both via load_scalar_table). Fixes the +2 uint16
     * stride byte-swap that otherwise breaks the entry walk cross-target
     * (one-fixture-on-Amiga bug). See fixup_cache_strides_on_load. */
    fixup_cache_strides_on_load(src, src_size);

    PROF_START(comp_slots);
    /* Apply the cross-target competition-buffer override AFTER the raw
     * blobs have populated competitionFileBuffer — it corrects the
     * struct-correct positions (league standings) that the blobs place
     * wrong on a cross-target load. Same-target: rewrites identical bytes
     * (VERIFY clean). See load_competition_overrides. */
    load_competition_overrides(src, src_size);

    /* Unified typed competition slots — applied AFTER the raw blobs +
     * the league override, overwriting the struct-correct header/team-id
     * positions for all 5 slots with the typed values (correct across
     * 32/64-bit + LE/BE). Same-target: identical values (VERIFY clean).
     * The "comp_slots" object is keyed primary/secondary/tertiary/diy/euro;
     * absent in pre-serializer saves → each load_comp_slot is a no-op. */
    {
        const char *cs = NULL; int cslen = 0;
        if (mjson_find(src, src_size, "$.comp_slots", &cs, &cslen)
                == MJSON_TOK_OBJECT) {
            load_comp_slot(cs, cslen, "primary",
                           (DIYCompetitionEx *)&_primaryCompetitionBuf);
            load_comp_slot(cs, cslen, "secondary",
                           (DIYCompetitionEx *)&_competitionContextBuf);
            load_comp_slot(cs, cslen, "tertiary",
                           (DIYCompetitionEx *)&_tertiaryCompetitionContextBuf);
            load_comp_slot(cs, cslen, "diy",
                           (DIYCompetitionEx *)&_diyCompetitionContextBuf);
            load_comp_slot(cs, cslen, "euro",
                           (DIYCompetitionEx *)&_euroCupsCompetitionContextBuf);
        }
    }

    /* TYPE-2 tournament current-STAGE restore (cross-target) — mirrors the
     * preset load_competition_buf. Re-lay the stage at competitionFileBuffer's
     * stageSnapshot typed/endian-neutral over the raw blob BEFORE
     * RebuildPointersAfterLoad copies it into diyCompetitionStart. League:
     * stats + teamSelOffset reconstructed = i; cup: the three knockout score arrays. The
     * cup teamOffsetTable is rebuilt careerTeam-aware below, after the
     * selected_teams load (it needs selTeamsPtr). Absent in pre-fix saves ->
     * mjson_find misses -> no-op, blob bytes stand. */
    if (((const DIYCompetitionEx *)&_primaryCompetitionBuf)
            ->base.competitionType == 2) {
        DIYCompetitionEx *stage = (DIYCompetitionEx *)
            ((uint8_t *)&_primaryCompetitionBuf + offsetof(CompetitionSlotBuffer, stageSnapshot));
        load_comp_slot(src, src_size, "comp_slot_stage", stage);
        load_stage_cupfull(src, src_size, "comp_slot_stage", stage,
                           ((const DIYCompetitionEx *)&_primaryCompetitionBuf)->base.numTeams);
        if (diy_stage_is_league(stage)) {
            const char *rows = NULL; int rows_len = 0;
            if (mjson_find(src, src_size, "$.stage_standings", &rows, &rows_len)
                    == MJSON_TOK_ARRAY) {
                int pos = 0, voff, vlen, vt; uint16_t i = 0;
                while (i < _primaryCompetitionBuf.tourn.header.numTeams && i < 64 &&
                       (pos = fast_array_next(rows, rows_len, pos,
                                              &voff, &vlen, &vt)) != 0) {
                    if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                    const char *e = rows + voff; int el = vlen;
                    long gp = 0, w = 0, d = 0, l = 0, gf = 0, ga = 0, pts = 0;
                    mjson_get_long(e, el, "$.gp",  &gp);
                    mjson_get_long(e, el, "$.w",   &w);
                    mjson_get_long(e, el, "$.d",   &d);
                    mjson_get_long(e, el, "$.l",   &l);
                    mjson_get_long(e, el, "$.gf",  &gf);
                    mjson_get_long(e, el, "$.ga",  &ga);
                    mjson_get_long(e, el, "$.pts", &pts);
                    DIYTeamStats *r = diy_ovr_row(stage, i);
                    r->teamSelOffset = (int32_t)i; /* identity = selTeamsPtr team index i (step-4 phase 3) */
                    r->gamesPlayed  = (uint16_t)gp;
                    r->wins         = (uint16_t)w;
                    r->draws        = (uint16_t)d;
                    r->losses       = (uint16_t)l;
                    r->goalsFor     = (uint16_t)gf;
                    r->goalsAgainst = (uint16_t)ga;
                    r->points       = (uint16_t)pts;
                    i++;
                }
            }
        } else {
            uint16_t *dsts[3] = {
                (uint16_t *)&stage->matchData.cup.cupRoundResults[0x40],
                stage->cupAggregateScores,
                (uint16_t *)&stage->leagueSectionState.phaseStartMonths[0],
            };
            const char *keys[3] = { "$.stage_score_tab", "$.stage_score_agg",
                                    "$.stage_score_pen" };
            for (int a = 0; a < 3; a++) {
                const char *arr = NULL; int alen = 0;
                if (mjson_find(src, src_size, keys[a], &arr, &alen) != MJSON_TOK_ARRAY)
                    continue;
                int pos = 0, voff, vlen, vt, i = 0;
                while (i < 18 &&
                       (pos = fast_array_next(arr, alen, pos,
                                              &voff, &vlen, &vt)) != 0) {
                    if (vt == MJSON_TOK_NUMBER)
                        dsts[a][i] = (uint16_t)parse_long_slice(arr + voff, vlen);
                    i++;
                }
            }
        }
    }

    /* TYPE-2 *Context slots' current stage — cross-target fix (see load_ctx_stage).
     * Mirrors the primary comp_slot_stage load above. Slot competitionType is
     * already loaded (comp_slots block) and the raw blob already applied, so the
     * typed stage overwrites the mis-positioned blob bytes. No-op for non-type-2
     * slots and pre-fix saves. */
    load_ctx_stage((const char *)src, (int)src_size, "secondary_comp_slot_stage",
                   (DIYCompetitionEx *)&_competitionContextBuf);
    load_ctx_stage((const char *)src, (int)src_size, "tertiary_comp_slot_stage",
                   (DIYCompetitionEx *)&_tertiaryCompetitionContextBuf);
    load_ctx_stage((const char *)src, (int)src_size, "diy_comp_slot_stage",
                   (DIYCompetitionEx *)&_diyCompetitionContextBuf);
    load_ctx_stage((const char *)src, (int)src_size, "euro_comp_slot_stage",
                   (DIYCompetitionEx *)&_euroCupsCompetitionContextBuf);

    /* TYPE-0 *Context slots' league standings — cross-target fix (see
     * load_ctx_league_standings). Slot competitionType is loaded and the raw
     * blob already applied, so the typed rows overwrite the mis-positioned
     * (8-byte-shifted) blob bytes. No-op for non-type-0 slots / pre-fix saves. */
    load_ctx_league_standings((const char *)src, (int)src_size, "secondary",
                   (DIYCompetitionEx *)&_competitionContextBuf);
    load_ctx_league_standings((const char *)src, (int)src_size, "tertiary",
                   (DIYCompetitionEx *)&_tertiaryCompetitionContextBuf);
    load_ctx_league_standings((const char *)src, (int)src_size, "diy",
                   (DIYCompetitionEx *)&_diyCompetitionContextBuf);
    load_ctx_league_standings((const char *)src, (int)src_size, "euro",
                   (DIYCompetitionEx *)&_euroCupsCompetitionContextBuf);
    PROF_END(comp_slots, "career-load");

    /* Saves from before 2026-10-28: the league offsets by global team number had the
     * original's 2000 entries; the rest of the table (GLOBAL_TEAM_NUMBERS) is 0, no offset. */
    {
        const char *arr = NULL; int alen = 0, n = 0;
        if (mjson_find(src, src_size, "$.career_scalars.diy_league_season_state_table",
                       &arr, &alen) == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt;
            while ((pos = fast_array_next(arr, alen, pos, &voff, &vlen, &vt)) != 0)
                n++;
        }
        for (int i = n > 0 ? n : GLOBAL_TEAM_NUMBERS; i < GLOBAL_TEAM_NUMBERS; i++)
            _diyLeagueSeasonStateTable[i] = 0;
    }

    /* Saves from before 2026-10-27: careerLastMatchIndexByCompetitionSlot (indexed by
     * compId) was 162 bytes, and the Copa Libertadores was compId 128 -- the Hungarian
     * cup's too. The array is now one byte per compId (the rest: 0xFF, as a new career
     * has them), and a Copa under way in the South-American quaternary slot
     * (_euroCupSlotIndex 3, which only the Copa sets) moves to 162 with its entry. */
    {
        const char *arr = NULL; int alen = 0, n = 0;
        if (mjson_find(src, src_size, "$.career_scalars.career_last_match_index_by_competition_slot",
                       &arr, &alen) == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt;
            while ((pos = fast_array_next(arr, alen, pos, &voff, &vlen, &vt)) != 0)
                n++;
        }
        if (n > 0 && n < (int)sizeof(_careerLastMatchIndexByCompetitionSlot)) {
            for (int i = n; i < (int)sizeof(_careerLastMatchIndexByCompetitionSlot); i++)
                _careerLastMatchIndexByCompetitionSlot[i] = 0xFF;
            DIYCompetition *q = &((DIYCompetitionEx *)&_euroCupsCompetitionContextBuf)->base;
            if (_euroCupSlotIndex == 3 && q->field_2E == 128) {
                q->field_2E = COMPID_copaLibertadores;
                _careerLastMatchIndexByCompetitionSlot[COMPID_copaLibertadores] =
                    _careerLastMatchIndexByCompetitionSlot[128];
                _careerLastMatchIndexByCompetitionSlot[128] = 0xFF;
            }
        }
    }

    PROF_START(team_ce);
    load_team_file_ex(src, src_size, "$.career_team",  &_careerTeam);
    load_team_file_ex(src, src_size, "$.current_team", &_currentTeam);
    PROF_END(team_ce, "career-load");

    /* numSelectedTeams comes through career_scalars
     * (num_selected_teams_inner aliases &numSelectedTeams). Now use
     * it to size the selected_teams loop. Slice the array once so
     * per-team lookups scan a single team's ~3 KB slice instead of
     * the full file each time. */
    PROF_START(selected_teams);
    const char *teams_arr = NULL;
    int teams_arr_len = 0;
    if (mjson_find(src, src_size, "$.selected_teams",
                   &teams_arr, &teams_arr_len) == MJSON_TOK_ARRAY) {
        int pos = 0, voff, vlen, vt;
        uint16_t i = 0;
        while (i < _numSelectedTeams && i < SELECTED_TEAMS_MAX &&
               (pos = fast_array_next(teams_arr, teams_arr_len, pos,
                                      &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) { i++; continue; }
            load_team_file_ex_slice(teams_arr + voff, vlen,
                                    (uint8_t *)(_selectedTeams + i));
            i++;
        }
    }
    PROF_END(selected_teams, "career-load");

    /* TYPE-2 tournament KNOCKOUT teamOffsetTable rebuild (cross-target) —
     * mirrors the preset path. teamOffsetTable[k] is a derived selTeamsPtr
     * byte-offset cache that byte-swaps + is layout-dependent cross-target;
     * rebuild it from cupTeamIds via VerifyTeamLoaded now that selTeamsPtr
     * (selected_teams), careerTeam and selTeamNumber are all loaded. For a
     * managed national team (careerTeam) findTeamInCache returns &careerTeam,
     * giving the correct NEGATIVE offset — so the R16 bracket names resolve
     * correctly cross-target. League stages skip this (standings rebuilt
     * earlier). Runs before RebuildPointersAfterLoad copies the stage. */
    if (((const DIYCompetitionEx *)&_primaryCompetitionBuf)
            ->base.competitionType == 2) {
        DIYCompetitionEx *stage = (DIYCompetitionEx *)
            ((uint8_t *)&_primaryCompetitionBuf + offsetof(CompetitionSlotBuffer, stageSnapshot));
        if (!diy_stage_is_league(stage)) {
            uint16_t n = _primaryCompetitionBuf.tourn.header.numTeams; if (n > 51) n = 51;
            for (uint16_t k = 0; k < n; k++) {
                uint16_t teamNum = BE_READ_U16(&stage->cupTeamIds[k]);
                if (teamNum == 0) continue;
                TeamFileEx *tp = VerifyTeamLoaded(teamNum);
                stage->bracketScratch.teamOffsetTable[k] =
                    diy_team_to_identity((uint8_t *)tp);  /* identity: -1 careerTeam / index */
            }
        }
    }

    /* TMD team pool — count from tmd_team_count scalar (already applied
     * by load_scalar_table above). Empty trailing slots stay zero from
     * the careerFileBufferBuf memset earlier in SaveJson_LoadCareer. */
    PROF_START(tmd_teams);
    {
        const char *tmd_arr = NULL;
        int tmd_arr_len = 0;
        if (mjson_find(src, src_size, "$.tmd_teams",
                       &tmd_arr, &tmd_arr_len) == MJSON_TOK_ARRAY) {
            uint16_t tmd_count = *(uint16_t *)&_careerFileBuffer;
            int pos = 0, voff, vlen, vt;
            uint16_t i = 0;
            while (i < tmd_count &&
                   (pos = fast_array_next(tmd_arr, tmd_arr_len, pos,
                                          &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                load_team_file_ex_slice(tmd_arr + voff, vlen,
                                        (uint8_t *)((TeamFileEx *)_tmdFileBuffer + i));
                i++;
            }
        }
    }
    PROF_END(tmd_teams, "career-load");

    /* A world's pools (team_pool.c); a save without them: every slot its own. An older
     * save has one, "team_pool" (Italy's). */
    TeamPool_Reset();
    {
        const char *tp = NULL;
        int tplen = 0;
        if (mjson_find(src, src_size, "$.team_pools", &tp, &tplen) == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt, k = 0;
            while (k < TEAM_POOL_FILES && (pos = fast_array_next(tp, tplen, pos, &voff, &vlen, &vt)) != 0)
                load_team_pool(tp + voff, vlen, &g_teamPool[k++]);
        } else if (mjson_find(src, src_size, "$.team_pool", &tp, &tplen) == MJSON_TOK_OBJECT) {
            load_team_pool(tp, tplen, &g_teamPool[0]);
        }
    }
    {
        extern uint16_t _teamsLoaded;
        _teamsLoaded = 0;       /* (a pooled file: read again with this career's identities) */
    }

    /* A world's cups (world_cups.c); a save without it: its first season. */
    WorldCups_Reset();
    {
        const char *wc = NULL;
        int wclen = 0;
        long v = 0;
        if (mjson_find(src, src_size, "$.world_cups", &wc, &wclen) == MJSON_TOK_OBJECT) {
            if (mjson_get_long(wc, wclen, "$.seasons", &v) > 0)
                g_worldCups.seasons = (uint8_t)v;
            if (mjson_get_long(wc, wclen, "$.league_cup_winner", &v) > 0)
                g_worldCups.leagueCupWinner = (uint16_t)v;
            if (mjson_get_long(wc, wclen, "$.league_cup_season", &v) > 0)
                g_worldCups.leagueCupSeason = (uint8_t)v;
            if (mjson_get_long(wc, wclen, "$.places_season", &v) > 0)
                g_worldCups.placesSeason = (uint8_t)v;
            if (mjson_get_long(wc, wclen, "$.places_country", &v) > 0)
                g_worldCups.placesCountry = (uint8_t)v;
            const char *arr; int al, pos = 0, voff, vlen, vt, i = 0;
            if (mjson_find(wc, wclen, "$.places", &arr, &al) == MJSON_TOK_ARRAY)
                while (i < 256 && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0)
                    if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                        g_worldCups.place[i++] = (uint8_t)v;
        }
    }

    /* A world's Coppa Italia Serie D (world_cupd.c); a save without it: not started. */
    CupD_Reset();
    {
        const char *cd = NULL;
        int cdlen = 0;
        if (mjson_find(src, src_size, "$.world_cupd", &cd, &cdlen) == MJSON_TOK_OBJECT) {
            WorldCupDState *d = &g_worldCupD;
            long v = 0;
            const char *arr; int al, pos, voff, vlen, vt, i;
            if (mjson_get_long(cd, cdlen, "$.season", &v) > 0) d->season = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.round", &v) > 0) d->round = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.drawn", &v) > 0) d->drawn = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.built", &v) > 0) d->built = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.tie", &v) > 0) d->bt = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.week1", &v) > 0) d->d1 = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.week2", &v) > 0) d->d2 = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.legs", &v) > 0) d->legs = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.team_a", &v) > 0) d->teamA = (uint16_t)v;
            if (mjson_get_long(cd, cdlen, "$.team_b", &v) > 0) d->teamB = (uint16_t)v;
            if (mjson_get_long(cd, cdlen, "$.generated", &v) > 0) d->gen = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.preliminary_ties", &v) > 0) d->np = (uint8_t)v;
            if (mjson_get_long(cd, cdlen, "$.slot2", &v) > 0) d->slot2 = (uint8_t)v;
            if (mjson_find(cd, cdlen, "$.sides", &arr, &al) == MJSON_TOK_ARRAY)
                for (pos = 0, i = 0; i < 2 * (WORLD_CUPD_PRE + WORLD_CUPD_R1) &&
                     (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0; )
                    if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                        d->side[i++] = (uint16_t)v;
            static const char *const keys[3] = { "$.drawn_ties", "$.results", "$.hosts" };
            uint8_t *const dst[3] = { d->sd, d->res, d->home };
            const int cap[3] = { 4, WORLD_CUPD_TIES, WORLD_CUPD_TIES };
            for (int k = 0; k < 3; k++)
                if (mjson_find(cd, cdlen, keys[k], &arr, &al) == MJSON_TOK_ARRAY)
                    for (pos = 0, i = 0; i < cap[k] && (pos = fast_array_next(arr, al, pos, &voff, &vlen, &vt)) != 0; )
                        if (mjson_get_long(arr + voff, vlen, "$", &v) > 0)
                            dst[k][i++] = (uint8_t)v;
        }
    }

    /* A world's play-off under way (world_playoff.c); slot 4 follows it
     * (WorldPlayoff_RestoreSlot, in the pointer rebuild). */
    WorldPlayoff_Reset();
    {
        const char *wp = NULL;
        int wplen = 0;
        if (mjson_find(src, src_size, "$.world_playoff", &wp, &wplen) == MJSON_TOK_OBJECT) {
            WorldPlayoffState *w = &g_worldPlayoff;
            uint16_t rows[2 * WORLD_PLAYOFF_EARLY_ROWS * 8];
            long v = 0;
            int nr;
            if (mjson_get_long(wp, wplen, "$.kind", &v) > 0) w->kind = (uint8_t)v;
            if (mjson_get_long(wp, wplen, "$.etp", &v) > 0) w->etp = (uint8_t)v;
            if (mjson_get_long(wp, wplen, "$.rules", &v) > 0) w->rules = (uint8_t)v;
            if (mjson_get_long(wp, wplen, "$.final_rules", &v) > 0) w->rules2 = (uint8_t)v;
            if (mjson_get_long(wp, wplen, "$.winner", &v) > 0) w->winner = (uint16_t)v;
            if (mjson_get_long(wp, wplen, "$.cup", &v) > 0) w->cup = (uint8_t)v;
            w->n = (uint8_t)wp_load_u16(wp, wplen, "$.teams", w->team, WORLD_PLAYOFF_MAX);
            wp_load_u8(wp, wplen, "$.places", w->prio, WORLD_PLAYOFF_MAX);
            wp_load_u16(wp, wplen, "$.points", w->pts, WORLD_PLAYOFF_MAX);
            wp_load_u16(wp, wplen, "$.won", w->won, WORLD_PLAYOFF_MAX);
            wp_load_u16(wp, wplen, "$.scored", w->gf, WORLD_PLAYOFF_MAX);
            wp_load_u8(wp, wplen, "$.table_places", w->place, WORLD_PLAYOFF_MAX);
            wp_load_u8(wp, wplen, "$.groups", w->group, WORLD_PLAYOFF_MAX);
            w->ties = (uint8_t)wp_load_u8(wp, wplen, "$.tie_a", w->tieA, 16);
            wp_load_u8(wp, wplen, "$.tie_b", w->tieB, 16);
            w->early = (uint8_t)wp_load_u8(wp, wplen, "$.early_divisions", w->earlyDiv, 2);
            wp_load_u8(wp, wplen, "$.early_counts", w->earlyN, 2);
            nr = wp_load_u16(wp, wplen, "$.early_rows", rows, 2 * WORLD_PLAYOFF_EARLY_ROWS * 8);
            for (int e = 0, i = 0; e < w->early && e < 2; e++)
                for (int k = 0; k < w->earlyN[e] && k < WORLD_PLAYOFF_EARLY_ROWS && i + 8 <= nr; k++) {
                    WorldPlayoffRow *r = &w->earlyRow[e][k];
                    r->team = rows[i++]; r->played = rows[i++]; r->won = rows[i++]; r->drawn = rows[i++];
                    r->lost = rows[i++]; r->gf = rows[i++]; r->ga = rows[i++]; r->pts = rows[i++];
                }
            if (w->kind == WORLD_PLAYOFF_SPEC) {
                uint8_t id[5] = { 0, 0, 0xFF, 0, 0 };
                wp_load_u8(wp, wplen, "$.spec", id, 5);
                w->country = id[0]; w->pidx = id[1]; w->stage = id[2]; w->season = id[3]; w->week = id[4];
                for (int t = 0; t < WORLD_PLAYOFF_MAX; t++) w->tw[t] = w->tl[t] = 0xFF;
                wp_load_u8(wp, wplen, "$.tie_winners", w->tw, WORLD_PLAYOFF_MAX);
                wp_load_u8(wp, wplen, "$.tie_losers", w->tl, WORLD_PLAYOFF_MAX);
                wp_load_u8(wp, wplen, "$.round_ties", w->tieT, 16);
                wp_load_u16(wp, wplen, "$.global_numbers", w->glob, WORLD_PLAYOFF_MAX);
            }
        }
    }
    WorldPlayoff_ResetStageTitles();
    {
        const char *sto = NULL;
        int stolen = 0;
        if (mjson_find(src, src_size, "$.stage_titles", &sto, &stolen) == MJSON_TOK_OBJECT) {
            uint16_t st[3 * 6] = { 0 };
            wp_load_u16(sto, stolen, "$.stages", st, 3 * 6);
            for (int k = 0; k < 3; k++) {
                WorldStageTitle *x = &g_worldStageTitle[k];
                x->season = (uint8_t)st[6 * k]; x->done = (uint8_t)st[6 * k + 1];
                x->team[0] = st[6 * k + 2]; x->team[1] = st[6 * k + 3];
                x->glob[0] = st[6 * k + 4]; x->glob[1] = st[6 * k + 5];
            }
        }
    }

    /* A world's UEFA coefficients and qualifying bracket (uefa_coef.c); a save without them:
     * the seeds at their first use. */
    UefaCoef_Reset();
    {
        const char *uc = NULL;
        int uclen = 0;
        long v = 0;
        if (mjson_find(src, src_size, "$.uefa_coef", &uc, &uclen) == MJSON_TOK_OBJECT) {
            UefaCoefState *u = &g_uefaCoef;
            static uint16_t kc[UEFA_COEF_CLUBS * 7];
            uint16_t prev[3 * 16], fin[6];
            uint8_t *p = (uint8_t *)u;
            for (uint32_t i = 0; i < sizeof *u; i++)
                p[i] = 0;
            u->init = 1;
            if (mjson_get_long(uc, uclen, "$.ring", &v) > 0) u->ring = (uint8_t)(v % 5);
            if (mjson_get_long(uc, uclen, "$.season_closed", &v) > 0) u->done = (uint8_t)v;
            wp_load_u8(uc, uclen, "$.association_clubs", u->teams, 256);
            wp_load_u32(uc, uclen, "$.association_points", u->pts, 256);
            wp_load_u32(uc, uclen, "$.association_seasons", &u->hist[0][0], 5 * 256);
            int nk = wp_load_u16(uc, uclen, "$.clubs", kc, UEFA_COEF_CLUBS * 7);
            for (int i = 0; i + 7 <= nk; i += 7) {
                UefaClubCoef *k = &u->club[u->nClubs++];
                k->team = kc[i];
                k->pts = kc[i + 1];
                for (int j = 0; j < 5; j++)
                    k->hist[j] = kc[i + 2 + j];
            }
            wp_load_u8(uc, uclen, "$.previous_counts", u->prevN, 3);
            wp_load_u16(uc, uclen, "$.previous", prev, 3 * 16);
            wp_load_u16(uc, uclen, "$.finals", fin, 6);
            wp_load_u8(uc, uclen, "$.phase_ended", u->phaseEnded, 3);
            for (int c = 0; c < 3; c++) {
                if (u->prevN[c] > 16)
                    u->prevN[c] = 16;
                for (int i = 0; i < 16; i++)
                    u->prev[c][i] = prev[c * 16 + i];
                u->fin[c][0] = fin[2 * c];
                u->fin[c][1] = fin[2 * c + 1];
            }
        }
        const char *uq = NULL;
        int uqlen = 0;
        if (mjson_find(src, src_size, "$.uefa_qualifying", &uq, &uqlen) == MJSON_TOK_OBJECT) {
            struct uq_state *q = &g_uqState;
            static uint16_t br[NBR * 4];
            uint16_t gl[3 * 36];
            if (mjson_get_long(uq, uqlen, "$.mark", &v) > 0) q->mark = (uint16_t)v;
            if (mjson_get_long(uq, uqlen, "$.manager_club", &v) > 0) q->m = (uint16_t)v;
            if (mjson_get_long(uq, uqlen, "$.stand_in", &v) > 0) q->s = (uint16_t)v;
            if (mjson_get_long(uq, uqlen, "$.stand_in_cup", &v) > 0) q->sc = (uint8_t)v;
            if (mjson_get_long(uq, uqlen, "$.ties_played", &v) > 0) q->k = (uint8_t)v;
            if (mjson_get_long(uq, uqlen, "$.results", &v) > 0) q->res = (uint8_t)v;
            wp_load_u8(uq, uqlen, "$.list_counts", q->gn, 3);
            wp_load_u16(uq, uqlen, "$.lists", gl, 3 * 36);
            for (int c = 0; c < 3; c++)
                for (int i = 0; i < 36; i++)
                    q->gl[c][i] = gl[c * 36 + i];
            int nb = wp_load_u16(uq, uqlen, "$.bracket", br, NBR * 4);
            q->nbr = 0;
            for (int i = 0; i + 4 <= nb; i += 4, q->nbr++) {
                q->br[q->nbr].r = (uint8_t)br[i];
                q->br[q->nbr].aw = (uint8_t)br[i + 1];
                q->br[q->nbr].a = br[i + 2];
                q->br[q->nbr].b = br[i + 3];
            }
        }
    }

    /* A world's stage tables (league_playoffs.c); a save without them: none kept. */
    LeaguePlayoffs_ResetStages();
    {
        const char *ls = NULL;
        int lslen = 0;
        if (mjson_find(src, src_size, "$.league_stages", &ls, &lslen) == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt, i = 0;
            static uint16_t rows[LEAGUE_STAGE_TABLES * 32 * 6];
            while (i < LEAGUE_STAGE_BANKS && (pos = fast_array_next(ls, lslen, pos, &voff, &vlen, &vt)) != 0) {
                const char *o = ls + voff;
                LeagueStageBank *b = &g_leagueStages[i++];
                long v = 0;
                b->used = 1;
                if (mjson_get_long(o, vlen, "$.country", &v) > 0) b->key = (uint8_t)v;
                if (mjson_get_long(o, vlen, "$.fresh", &v) > 0) b->fresh = (uint8_t)v;
                if (mjson_get_long(o, vlen, "$.kept_division", &v) > 0) b->keptDiv = (uint8_t)v;
                wp_load_u8(o, vlen, "$.clubs", b->n, LEAGUE_STAGE_TABLES);
                int nr = wp_load_u16(o, vlen, "$.tables", rows, LEAGUE_STAGE_TABLES * 32 * 6), at = 0;
                for (int k = 0; k < LEAGUE_STAGE_TABLES; k++) {
                    if (b->n[k] > 32) b->n[k] = 32;
                    for (int r = 0; r < b->n[k]; r++) {
                        LeagueStageRow *x = &b->r[k][r];
                        if (at + 6 > nr) { b->n[k] = (uint8_t)r; break; }
                        x->team = rows[at++]; x->glob = rows[at++]; x->won = rows[at++];
                        x->gf = rows[at++]; x->ga = rows[at++]; x->pts = rows[at++];
                    }
                }
                nr = wp_load_u16(o, vlen, "$.kept", rows, 32 * 6);
                b->nKept = 0;
                for (at = 0; at + 6 <= nr && b->nKept < 32; b->nKept++) {
                    LeagueStageRow *x = &b->kept[b->nKept];
                    x->team = rows[at++]; x->glob = rows[at++]; x->won = rows[at++];
                    x->gf = rows[at++]; x->ga = rows[at++]; x->pts = rows[at++];
                }
            }
        }
    }

    /* A world's other confederations' cups (world_conti.c); a save without them: none yet. */
    Conti_Reset();
#if SWOS_WORLDS
    {
        const char *co = NULL;
        int colen = 0;
        long v = 0;
        if (mjson_find(src, src_size, "$.conti_holders", &co, &colen) == MJSON_TOK_OBJECT) {
            static uint32_t hold[CT_NCUP * 4];
            if (mjson_get_long(co, colen, "$.mark", &v) > 0) CT_ST.mark = (u16)v;
            if (mjson_get_long(co, colen, "$.season", &v) > 0) CT_ST.season = (u16)v;
            for (int i = 0; i < CT_NCUP * 4; i++) hold[i] = 0;
            wp_load_u32(co, colen, "$.holders", hold, CT_NCUP * 4);
            for (int c = 0; c < CT_NCUP; c++) {
                CT_ST.hold[c].id = (u16)hold[4 * c]; CT_ST.hold[c].assoc = (u8)hold[4 * c + 1];
                CT_ST.hold[c].str = hold[4 * c + 2]; CT_ST.hold[c].pts = hold[4 * c + 3];
            }
        }
        if (mjson_find(src, src_size, "$.conti_edition", &co, &colen) == MJSON_TOK_OBJECT) {
            static uint16_t ent[CT_NED * 3];
            uint16_t res[16 * 3] = { 0 };
            if (mjson_get_long(co, colen, "$.mark", &v) > 0) CT_ED.mark = (u16)v;
            if (mjson_get_long(co, colen, "$.seed", &v) > 0) CT_ED.seed = (u32)v;
            if (mjson_get_long(co, colen, "$.manager_club", &v) > 0) CT_ED.mgr = (u16)v;
            if (mjson_get_long(co, colen, "$.results_count", &v) > 0) CT_ED.nf = (u8)(v > 16 ? 16 : v);
            const int ne = wp_load_u16(co, colen, "$.entrants", ent, CT_NED * 3);
            CT_ED.ne = 0;
            for (int i = 0; i + 3 <= ne; i += 3, CT_ED.ne++) {
                CT_ED.e[CT_ED.ne].id0 = (u8)ent[i]; CT_ED.e[CT_ED.ne].id1 = (u8)(ent[i] >> 8);
                CT_ED.e[CT_ED.ne].cr = (u8)ent[i + 1];
                CT_ED.e[CT_ED.ne].s0 = (u8)ent[i + 2]; CT_ED.e[CT_ED.ne].s1 = (u8)(ent[i + 2] >> 8);
            }
            wp_load_u16(co, colen, "$.results", res, 16 * 3);
            for (int i = 0; i < 16; i++) {
                CT_ED.f[i].c = (u8)res[3 * i]; CT_ED.f[i].r = (u8)res[3 * i + 1]; CT_ED.f[i].v = (u8)res[3 * i + 2];
            }
        }
    }
#endif

    /* Foreign-market player pool — fixed-size MarketPlayer[432]. */
    PROF_START(market_players);
    {
        const char *mp_arr = NULL;
        int mp_arr_len = 0;
        if (mjson_find(src, src_size, "$.career_foreign_market_players",
                       &mp_arr, &mp_arr_len) == MJSON_TOK_ARRAY) {
            int pos = 0, voff, vlen, vt;
            uint32_t i = 0;
            while (i < 432 &&
                   (pos = fast_array_next(mp_arr, mp_arr_len, pos,
                                          &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                load_market_player_body(mp_arr + voff, vlen,
                                        _careerForeignMarketPlayers + i * sizeof(MarketPlayer));
                i++;
            }
        }
    }
    PROF_END(market_players, "career-load");

    /* Transfer-offer + job-offer tables — typed arrays-of-objects (mirror of
     * the write side). Each table is a fixed 5 records. */
    {
        const struct { const char *path; uint8_t *base; size_t stride;
                       const StructFieldDesc *table; } offerTabs[3] = {
            { "$.transfer_offer_table", _transferOfferTable, sizeof(TransferOfferEntry), s_transfer_offer_fields },
            { "$.new_job_offers",       _newJobOffers,        sizeof(JobOffer),           s_job_offer_fields },
            { "$.job_offers",           _jobOffers,           sizeof(JobOffer),           s_job_offer_fields },
        };
        for (int t = 0; t < 3; t++) {
            const char *arr = NULL; int arr_len = 0;
            if (mjson_find(src, src_size, offerTabs[t].path, &arr, &arr_len) != MJSON_TOK_ARRAY)
                continue;
            int pos = 0, voff, vlen, vt;
            uint32_t i = 0;
            while (i < 5 && (pos = fast_array_next(arr, arr_len, pos, &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                load_struct_object(arr + voff, vlen,
                                   offerTabs[t].base + i * offerTabs[t].stride,
                                   offerTabs[t].table);
                i++;
            }
        }
    }

    /* Career / current team reserve players — 16 × PlayerFile each. */
    PROF_START(reserves);
    {
        const char *paths[2] = { "$.career_team_reserves", "$.current_team_reserves" };
        uint8_t *bases[2] = { (uint8_t *)&_careerTeam  + 684,
                              (uint8_t *)&_currentTeam + 684 };
        for (int t = 0; t < 2; t++) {
            const char *arr = NULL; int arr_len = 0;
            if (mjson_find(src, src_size, paths[t], &arr, &arr_len) != MJSON_TOK_ARRAY)
                continue;
            int pos = 0, voff, vlen, vt;
            int i = 0;
            while (i < 16 &&
                   (pos = fast_array_next(arr, arr_len, pos,
                                          &voff, &vlen, &vt)) != 0) {
                if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                load_player_file_body(arr + voff, vlen,
                                      bases[t] + (size_t)i * sizeof(PlayerFile));
                i++;
            }
        }
    }
    PROF_END(reserves, "career-load");
}

int16_t SaveJson_LoadCareer(uint8_t *filename, const uint8_t *src_unused,
                            uint32_t src_size_unused)
{
    /* JSON saves can be several hundred KB after base64 expansion —
     * larger than careerFileBuffer can hold as a destination — so
     * we ignore the legacy in-place `src` buffer and read the file
     * fresh from disk into a heap allocation sized to the file. */
    (void)src_unused; (void)src_size_unused;

    PROF_START(total);

    PROF_START(read_file);
    uint8_t *src = NULL;
    uint32_t src_size = 0;
    if (read_save_file_heap(filename, &src, &src_size) != 0)
        return 1;
    PROF_END(read_file, "career-load");
#ifdef SWOS_SAVE_FORMAT_JSON_PROFILE
    SWOS_DBG("[json-prof-career-load] file_size: %u bytes\n",
             (unsigned)src_size);
    SWOS_DBG_FLUSH();
#endif

    /* Verify schema_version. */
    long version = 0;
    if (mjson_get_long((const char *)src, (int)src_size,
                         "$.schema_version", &version) <= 0) {
        swos_io_free(src);
        return 1;
    }
    if ((int)version != SAVE_JSON_SCHEMA_VERSION) {
        swos_io_free(src);
        return 1;
    }

    /* Verify file_kind. */
    char kind[32] = {0};
    if (mjson_get_string((const char *)src, (int)src_size,
                         "$.file_kind", kind, sizeof(kind)) <= 0) {
        swos_io_free(src);
        return 1;
    }
    if (strcmp(kind, KIND_CAREER) != 0) {
        swos_io_free(src);
        return 1;
    }
    if (!save_world_ok(src, src_size)) {
        swos_io_free(src);
        return 1;
    }

    /* Clear the two surviving careerFileBuffer scalars before the typed load
     * repopulates them. The 256 KB careerFileBufferBuf overlay (which the
     * legacy load memset cleared whole) is RETIRED — every career field is now
     * a standalone typed struct loaded from named JSON, and careerFileBuffer
     * (TMD count) + numSelectedTeams are standalone uint16 globals. So there is
     * no longer a contiguous region to zero; just reset these two (the load's
     * tmd_team_count / num_selected_teams paths set them right after). */
    PROF_START(memset_buf);
    _careerFileBuffer = 0;
    _numSelectedTeams = 0;
    PROF_END(memset_buf, "career-load");

    PROF_START(apply_semantic);
    apply_semantic_load((const char *)src, (int)src_size);
    PROF_END(apply_semantic, "career-load");

    /* Re-derive all pointer fields and distribute the loaded primary
     * slot's bytes into diyCompetitionStart / diyFileBufferCopy. The
     * legacy .CAR path runs this same helper after the raw-byte load;
     * the JSON path now matches.
     *
     * Per-slot type bytes are restored via primary_slot_type /
     * secondary_slot_type / ... scalars in s_career_scalars (already
     * applied by apply_semantic_load above), so step 2 of the rebuild
     * dispatches to the correct (league/cup/hybrid) branch when it
     * reads slotPtr[0..1]. */
    PROF_START(rebuild_ptrs);
    extern void RebuildPointersAfterLoad(void);
    RebuildPointersAfterLoad();
    PROF_END(rebuild_ptrs, "career-load");

    swos_io_free(src);
    PROF_END(total, "career-load");
    return 0;
}

/* ---------- .SEA / .PRE / .DIY (preset / DIY competition) ----------
 *
 * A standalone competition file carries the PRIMARY competition slot (the
 * only competition it defines) plus the save-file header and team/tmd counts
 * — and `primaryCompetitionBuf.tourn.header.numTeams * 684` trailing
 * TeamFileEx records at &selTeamsPtr. Re-uses the careerFileBuffer
 * scalar/blob table (s_career_scalars) but emits only the entries tagged
 * comp=1 (FXC/FAC) — selected by IDENTITY, not by a runtime address span.
 * (The career save path emits the whole table; the comp tag only gates the
 * preset/DIY subset.)
 */

/* competitionFileBuffer retired → primaryCompetitionBuf (file-scope decl above; S6). */

/* Byte footprint of a FieldDesc's target: the type width for scalars, the
 * declared `size` for string/bytes/array fields. Used to zero exactly the
 * save-covered fields before a load (see load_competition_buf). */
static uint32_t field_byte_size(const FieldDesc *f)
{
    switch (f->type) {
    case FLD_U8:  case FLD_S8:        return 1;
    case FLD_U16: case FLD_S16:       return 2;
    case FLD_U32: case FLD_S32:       return 4;
    case FLD_STR: case FLD_BYTES:
    case FLD_U16_ARRAY: case FLD_U32_ARRAY: return f->size;
    default:                          return 0;
    }
}

#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
/* Per-FIELD round-trip verify: snapshot every saved field in `table` by its own
 * (&addr, byte size), zero them all, reload the just-written file once, then
 * diff each field against its snapshot. Does NOT restore the snapshot afterwards
 * (see the NON-RESTORE POLICY on verify_roundtrip_region): a partial restore
 * left the scalar table pre-save but the reload's runtime/derived state
 * (diyCompetitionStart, comp-slot caches, re-derived pointers) reloaded — a
 * desynced mix that crashed save-then-CONTINUE (0x6006). We leave the fully
 * reloaded state, which is exactly what a fresh load produces (playable); for a
 * clean verify the reloaded bytes equal the pre-save bytes, so authored state is
 * unchanged. The snapshot is kept only for the diff.
 *
 * Replaces the old single-region verify_roundtrip_region("career",
 * &careerFileBuffer, data_size). That region WAS the careerFileBufferBuf overlay,
 * but after the field-extraction untangle it holds only dead .skip zeros plus
 * the two surviving scalars (tmd count + numSelectedTeams) — every other saved
 * field is a standalone struct OUTSIDE the region — so the region diff verified
 * almost nothing (~zeros vs zeros). Walking the descriptor table verifies
 * EXACTLY the bytes the save captures, across every extracted struct (finances,
 * season record, status/trophy flags, settings, comp slots, market counters,
 * ...), and tracks the table automatically. A nonzero count is a real
 * save/load round-trip regression; reports the first mismatching field by name. */
static void verify_roundtrip_fields(const char *label,
                                    const FieldDesc *table,
                                    VerifyReloadFn reload,
                                    uint8_t *filename)
{
    uint32_t total = 0, nfields = 0;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        uint32_t sz = field_byte_size(f);
        if (sz) { total += sz; nfields++; }
    }
    if (!total) return;

    uint8_t *snap = (uint8_t *)swos_io_alloc(total);
    if (!snap) return;

    uint32_t off = 0;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        uint32_t sz = field_byte_size(f);
        if (!sz) continue;
        memcpy(snap + off, f->addr, sz);   /* snapshot the just-saved live bytes */
        memset(f->addr, 0, sz);            /* clear so the reload must restore it */
        off += sz;
    }

    reload(filename);

    size_t bad_fields = 0, bad_bytes = 0;
    off = 0;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        uint32_t sz = field_byte_size(f);
        if (!sz) continue;
        const uint8_t *live = (const uint8_t *)f->addr;
        const uint8_t *was  = snap + off;
        if (memcmp(live, was, sz) != 0) {
            uint32_t fb = 0, fo = 0; int got = 0;
            for (uint32_t i = 0; i < sz; i++)
                if (live[i] != was[i]) { fb++; if (!got) { fo = i; got = 1; } }

            /* EXEMPT the comp-slot whole-buffer FLD_BYTES blobs (the 5 *_context
             * dumps, sizeof(CompetitionSlotBuffer)). These raw region dumps are
             * REDUNDANT with the typed comp-slot serialization: the authoritative
             * load path is the blob THEN load_comp_slot / load_competition_overrides
             * re-laying typed fields + reconstructing derived caches
             * (bracketScratch.teamOffsetTable, stage standings rows) + re-deriving
             * transient header/state bytes on top. So the raw blob legitimately
             * diverges wherever the typed path recomputes a byte — observed across
             * the header (+0/+2) and the derived-cache region. The AUTHORED slot
             * state (competitionType, slot states, winner, name, counts, ...) is
             * each a TYPED entry in this same table and IS counted — all clean.
             * Cross-target the blobs are re-laid by the typed comp-slot serializer
             * (the validated career save path) + the dedicated comp-slot verify.
             * Log the diff for transparency but DON'T count it: a redundant-raw-dump
             * vs typed-path divergence is not an authored-state round-trip
             * regression. (Roadmap: promote these blobs to typed fields and drop
             * the raw dump entirely.) */
            if (f->type == FLD_BYTES && f->size == sizeof(CompetitionSlotBuffer)) {
                SWOS_DBG("[json-verify-%s]   (derived, exempt) '%s': "
                         "%u/%u bytes differ, first +%u\n",
                         label, f->name, (unsigned)fb, (unsigned)sz, (unsigned)fo);
            } else {
                bad_fields++; bad_bytes += fb;
                SWOS_DBG("[json-verify-%s]   field '%s': %u/%u bytes differ, "
                         "first +%u loaded=0x%02X saved=0x%02X\n",
                         label, f->name, (unsigned)fb, (unsigned)sz, (unsigned)fo,
                         (unsigned)live[fo], (unsigned)was[fo]);
            }
        }
        /* No restore — leave the reloaded value (NON-RESTORE POLICY above). */
        off += sz;
    }

    if (bad_fields == 0)
        SWOS_DBG("[json-verify-%s] 0 mismatches across %u fields (%u bytes) "
                 "[comp-slot derived blobs exempt]\n",
                 label, (unsigned)nfields, (unsigned)total);
    else
        SWOS_DBG("[json-verify-%s] %zu field(s) / %zu byte(s) mismatch\n",
                 label, bad_fields, bad_bytes);

    swos_io_free(snap);
}
#endif /* SWOS_SAVE_FORMAT_JSON_VERIFY */


static void emit_scalar_table_comp(mjson_print_fn_t fn, void *fnd,
                                   const char *section_key,
                                   const FieldDesc *table)
{
    mjson_printf(fn, fnd, "%Q:{", section_key);
    int first = 1;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        if (!f->comp) continue;
        if (!first) mjson_printf(fn, fnd, "%s", ",");
        first = 0;
        const uint8_t *p = (const uint8_t *)f->addr;
        switch (f->type) {
        case FLD_U8:
            mjson_printf(fn, fnd, "%Q:%u", f->name, (unsigned)*p); break;
        case FLD_S8:
            mjson_printf(fn, fnd, "%Q:%d", f->name, (int)*(int8_t *)p); break;
        case FLD_U16:
            mjson_printf(fn, fnd, "%Q:%u", f->name, (unsigned)*(uint16_t *)p); break;
        case FLD_S16:
            mjson_printf(fn, fnd, "%Q:%d", f->name, (int)*(int16_t *)p); break;
        case FLD_U32:
            mjson_printf(fn, fnd, "%Q:%lu", f->name, (long)(uint32_t)*(uint32_t *)p); break;
        case FLD_S32:
            mjson_printf(fn, fnd, "%Q:%ld", f->name, (long)*(int32_t *)p); break;
        case FLD_STR: {
            uint32_t n = 0;
            while (n < f->size && p[n] != '\0') n++;
            /* Zero post-NUL bytes so the save reflects what the
             * loader will round-trip back. Without this, garbage
             * left over from prior buffer use shows up as
             * verify-time mismatches even though it has no game
             * semantics. */
            for (uint32_t i = n; i < f->size; i++)
                ((uint8_t *)p)[i] = 0;
            mjson_printf(fn, fnd, "%Q:%.*Q", f->name, (int)n, (const char *)p);
            break;
        }
        case FLD_BYTES:
            mjson_printf(fn, fnd, "%Q:[", f->name);
            for (uint32_t i = 0; i < f->size; i++) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%u", (unsigned)p[i]);
            }
            mjson_printf(fn, fnd, "%s", "]");
            break;
        case FLD_U16_ARRAY:
            /* Endian-neutral decimal words — see emit_scalar_table. The
             * primary-competition header runs fall in the preset/DIY save
             * range too, so this path must handle them or .SEA/.DIY saves
             * silently drop the comp header (zeroed on load → panic). */
            mjson_printf(fn, fnd, "%Q:[", f->name);
            for (uint32_t i = 0; i + 1 < f->size; i += 2) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%u", (unsigned)*(uint16_t *)(p + i));
            }
            mjson_printf(fn, fnd, "%s", "]");
            break;
        case FLD_U32_ARRAY:
            mjson_printf(fn, fnd, "%Q:[", f->name);
            for (uint32_t i = 0; i + 3 < f->size; i += 4) {
                if (i > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%ld", (long)*(int32_t *)(p + i));
            }
            mjson_printf(fn, fnd, "%s", "]");
            break;
        default:
            break;
        }
    }
    mjson_printf(fn, fnd, "%s", "}");
}

static void load_scalar_table_comp(const char *buf, int buflen,
                                   const char *section_key,
                                   const FieldDesc *table)
{
    /* Slice the section once — same speedup pattern as
     * load_scalar_table. */
    char section_path[80];
    swos_io_snprintf(section_path, sizeof(section_path), "$.%s", section_key);
    const char *section = NULL; int section_len = 0;
    if (mjson_find(buf, buflen, section_path,
                   &section, &section_len) != MJSON_TOK_OBJECT)
        return;

#if MJSON_ENABLE_NEXT && !defined(SWOS_SAVE_LEGACY_DISPATCH)
    /* Walk-once hashed dispatch — same speedup load_scalar_table got, but
     * with the comp-tag filter applied at lookup. The legacy variant
     * below did one O(section_len) mjson_get_long per comp-tagged FieldDesc;
     * for s_career_scalars (242 entries) over the ~20 KB comp_scalars
     * section that is ~5 MB of byte scanning per preset/DIY load — the
     * single mjson_next pass is ~10-20× faster (this was the slow .PRE
     * load). Behaviour-identical: absent keys leave their target unchanged,
     * non-comp fields are skipped. Own index cache (same table as
     * load_scalar_table's, but a separate static is simplest + one-time). */
    static HashedField        s_range_index[300];
    static const FieldDesc   *s_range_table = NULL;
    static int                s_range_count = 0;
    if (s_range_table != table) {
        s_range_count = build_hashed_index(table, s_range_index,
                                           (int)(sizeof(s_range_index) /
                                                 sizeof(s_range_index[0])));
        if (s_range_count < 0) { s_range_table = NULL; return; }
        s_range_table = table;
    }

    char tmp[64];
    int pos = 0, koff, klen, voff, vlen, vt;
    while ((pos = mjson_next(section, section_len, pos,
                             &koff, &klen, &voff, &vlen, &vt)) != 0) {
        if (klen < 2) continue;
        const char *key  = section + koff + 1;
        int         keyl = klen - 2;
        uint32_t    h    = fnv1a_bytes(key, keyl);
        const FieldDesc *f = find_field_by_hash(h, s_range_index, s_range_count);
        if (!f) continue;
        if ((int)strlen(f->name) != keyl ||
            memcmp(f->name, key, (size_t)keyl) != 0) continue;
        if (!f->comp) continue;

        uint8_t    *p  = (uint8_t *)f->addr;
        const char *vs = section + voff;
        int         vl = vlen;
        switch (f->type) {
        case FLD_U8: case FLD_S8:
            if (vt == MJSON_TOK_NUMBER) *p = (uint8_t)parse_long_slice(vs, vl);
            break;
        case FLD_U16: case FLD_S16:
            if (vt == MJSON_TOK_NUMBER) *(uint16_t *)p = (uint16_t)parse_long_slice(vs, vl);
            break;
        case FLD_U32: case FLD_S32:
            if (vt == MJSON_TOK_NUMBER) *(uint32_t *)p = (uint32_t)parse_long_slice(vs, vl);
            break;
        case FLD_STR:
            memset(p, 0, f->size);
            if (vt == MJSON_TOK_STRING &&
                mjson_get_string(vs, vl, "$", tmp, (int)sizeof(tmp)) > 0) {
                uint32_t n = (uint32_t)strlen(tmp);
                if (n > f->size) n = f->size;
                memcpy(p, tmp, n);
            }
            break;
        case FLD_BYTES:
            if (vt == MJSON_TOK_ARRAY) parse_byte_array_slice(vs, vl, p, f->size);
            break;
        case FLD_U16_ARRAY:
            if (vt == MJSON_TOK_ARRAY) parse_u16_array_slice(vs, vl, p, f->size);
            break;
        case FLD_U32_ARRAY:
            if (vt == MJSON_TOK_ARRAY) parse_u32_array_slice(vs, vl, p, f->size);
            break;
        default:
            break;
        }
    }
#else
    char path[80];
    char tmp[64];
    long num = 0.0;
    for (const FieldDesc *f = table; f->type != FLD_END; f++) {
        if (!f->comp) continue;
        swos_io_snprintf(path, sizeof(path), "$.%s", f->name);
        uint8_t *p = (uint8_t *)f->addr;
        switch (f->type) {
        case FLD_U8: case FLD_S8:
            if (mjson_get_long(section, section_len, path, &num) > 0) *p = (uint8_t)(int)num;
            break;
        case FLD_U16: case FLD_S16:
            if (mjson_get_long(section, section_len, path, &num) > 0) *(uint16_t *)p = (uint16_t)(int)num;
            break;
        case FLD_U32: case FLD_S32:
            if (mjson_get_long(section, section_len, path, &num) > 0) *(uint32_t *)p = (uint32_t)(int32_t)num;
            break;
        case FLD_STR:
            memset(p, 0, f->size);
            if (mjson_get_string(section, section_len, path, tmp, sizeof(tmp)) > 0) {
                uint32_t n = (uint32_t)strlen(tmp);
                /* Fields can fill all `f->size` bytes — see comment
                 * in load_str_at. Don't reserve a NUL slot. */
                if (n > f->size) n = f->size;
                memcpy(p, tmp, n);
            }
            break;
        case FLD_BYTES:
            load_bytes_at(section, section_len, path, p, 0, f->size);
            break;
        case FLD_U16_ARRAY: {
            /* Endian-neutral decimal words — preset/DIY (.SEA/.DIY) load
             * companion to emit_scalar_table_comp. Without this the
             * primary-competition header runs load as zero on those save
             * kinds → panic on first use. */
            const char *arr = NULL;
            int arr_len = 0;
            if (mjson_find(section, section_len, path,
                           &arr, &arr_len) == MJSON_TOK_ARRAY)
                parse_u16_array_slice(arr, arr_len, p, f->size);
            break;
        }
        case FLD_U32_ARRAY: {
            const char *arr = NULL;
            int arr_len = 0;
            if (mjson_find(section, section_len, path,
                           &arr, &arr_len) == MJSON_TOK_ARRAY)
                parse_u32_array_slice(arr, arr_len, p, f->size);
            break;
        }
        default:
            break;
        }
    }
#endif
}

extern TeamFileEx *VerifyTeamLoaded(uint16_t teamNum);

/* A type-2 tournament's CURRENT stage is a group (league) or a knockout
 * (cup). The reliable discriminator is the current slot's isLeagueFormat in
 * the tournament master (nonzero = league/group, 0 = cup/knockout) — but it
 * must be indexed by the ACTIVE STAGE INDEX, which the game keeps in the
 * master header's `roundComplete` (0x54). That is exactly how the game itself
 * selects the stage in DIY_LoadCurrentCompetitionSlot:
 *     slotIdx = diyFileBufferCopy.header.roundComplete;  // 0x54
 *     s = slots[slotIdx];  if (s->isLeagueFormat) {league} else {cup}
 *
 * The earlier version indexed slots[] with the STAGE's own currentRoundIndex
 * (0x58) — a WITHIN-stage round counter, not the stage index. That collides at
 * the group<->knockout boundary: a cup stage before its first round has
 * currentRoundIndex==0 -> slots[0]==group -> misread as league (dumped the cup
 * bracketScratch as garbage standings), and a group on matchday 2 has
 * currentRoundIndex==1 -> slots[1]==cup -> misread as cup. It only worked for
 * an R16 save taken *after* a knockout round, where the counter coincidentally
 * equalled the stage index (=1). roundComplete is the clean stage index in both
 * the save buffer (decode: master round_complete=1 for an R16) and on load
 * (it round-trips in the master "comp_slot" via emit/load_comp_slot), so this
 * is valid on both sides. Defaults to league if out of range. The `stage` arg
 * is kept for call-site symmetry but the decision is master-driven. */
/* league-format detection for a tournament's ACTIVE stage, from its master's
 * roundComplete (active stage index) -> slots[r].isLeagueFormat. Parameterised
 * by the master so it works per-context (the euro/secondary/... slots' own
 * tourn), not only the primary. isLeagueFormat is tested != 0 so the result is
 * robust even if that uint16 rode the raw blob byte-swapped cross-target
 * (1 <-> 0x100 are both nonzero); roundComplete for a group/first-round stage
 * is 0 (byte-swap-safe). */
static int diy_stage_is_league_master(const DIYTournamentState *master)
{
    int16_t r = master->header.roundComplete;   /* 0x54: active stage index */
    if (r < 0 || r >= 6) return 1;
    return master->slots[r].isLeagueFormat != 0;
}

static int diy_stage_is_league(const DIYCompetitionEx *stage)
{
    (void)stage;
    return diy_stage_is_league_master(
        (const DIYTournamentState *)&_primaryCompetitionBuf);
}

/* Shared write body for preset / DIY — they share competitionFileBuffer
 * layout, only the file_kind string differs. */
static int16_t write_competition_buf(uint8_t *filename, const char *kind)
{
    JsonOutBuf out = { NULL, 0, 0, 0 };

    /* Pool reset — see comment in SaveJson_WriteCareer. */
    string_pool_reset();

    mjson_printf(json_out_emit, &out,
        "{\n  %Q:%d,\n  %Q:%Q,\n  %Q:%Q,\n  %Q:%u,\n  ",
        "schema_version", SAVE_JSON_SCHEMA_VERSION,
        "file_kind",      kind,
        "world",          World_Tag(),
        "num_diy_teams",  (unsigned)_primaryCompetitionBuf.tourn.header.numTeams);

    /* primaryCompStringOffset is intentionally NOT saved — it's a
     * stale absolute pointer into the strings table that the legacy
     * format dumped verbatim. The career load path zeroes it in
     * Rebuild_CompStringOffsets and InitializeNewSeason tolerates
     * the resulting empty string in the season-end summary. Preset/
     * DIY loads land in the same place after load_competition_buf
     * zeroes the comp-tagged fields, so nothing here needs to write it. */
    emit_scalar_table_comp(json_out_emit, &out,
                           "comp_scalars", s_career_scalars);

    /* Typed primary competition slot — competitionFileBuffer IS the
     * primary DIYCompetitionEx. The .PRE/.DIY region only carries this
     * one slot (the other 4 career contexts aren't in range), so emit it
     * the same way SaveJson_WriteCareer does for "primary". This gives
     * preset/DIY saves the cross-target-safe header + team-id + cup +
     * TOURNAMENT-slot coverage (emit_comp_slot's type-2 branch), instead
     * of leaving the type-2 DIYTournamentSlot uint16 fields to byte-swap
     * on the raw comp blob. Loaded in parallel with the blob (VERIFY
     * proves identical same-target bytes), mirroring the career path. */
    mjson_printf(json_out_emit, &out, ",\n  ");
    emit_comp_slot(json_out_emit, &out, "comp_slot",
                   (const DIYCompetitionEx *)&_primaryCompetitionBuf);

    /* Cross-target league-standings override (teamSelOffset -> team identity),
     * the SAME endian-neutral re-lay the career path uses (SaveJson_WriteCareer).
     * A .DIY/.PRE LEAGUE's 64 standings rows otherwise ride ONLY the raw
     * primary_competition_context FLD_BYTES blob, whose int32 teamSelOffset
     * byte-swaps on a LE<->BE (SDL->Amiga) load — every row then resolves to the
     * wrong/blank team and the DIY league table is garbage cross-target (the
     * career path was immune only because it already emitted this override). For
     * non-league comps this emits just {type} (load is a no-op). */
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_competition_overrides(json_out_emit, &out);

    /* TYPE-2 tournament current-STAGE slot. A WORLD CUP / EUROPEAN
     * CHAMPIONSHIP keeps a *second* DIYCompetitionEx for the stage in
     * progress — a league during the group phase, a cup during the
     * knockout — embedded at competitionFileBuffer's stageSnapshot (Phase B;
     * was the legacy +0x420 overlay; diy_competition.c's diy_*_to_* keep it in
     * sync with diyCompetitionStart). The primary "comp_slot" above is the
     * tournament MASTER (DIYTournamentState: team-id pool + slot records),
     * NOT this stage, so the stage rode the raw comp_scalars blob and
     * byte-swapped on a LE<->BE load: live-gdb confirmed numTeams read 4096
     * (= swap of 16, an R16) and bracketScratch.teamOffsetTable[i] read
     * 0xAC020000 (= swap of 684, the selTeamsPtr stride) -> garbage team
     * pointers -> the garbage WORLD CUP bracket names. Emit it as its own
     * typed slot: emit_comp_slot branches on the stage's OWN type (cup=1 ->
     * symbolic team_offset_table + struct-anchored grid/scores; league=0 ->
     * team_entry_offsets), and the endian-neutral header fixes numTeams/type
     * for either phase. Struct-anchored, so it also corrects the 64-bit
     * namePtr offset shift. Only tournaments carry this stage (a plain
     * league/cup preset IS the primary slot), so gate on the master type. */
    if (((const DIYCompetitionEx *)&_primaryCompetitionBuf)
            ->base.competitionType == 2) {
        const DIYCompetitionEx *stage = (const DIYCompetitionEx *)
            ((const uint8_t *)&_primaryCompetitionBuf + offsetof(CompetitionSlotBuffer, stageSnapshot));
        mjson_printf(json_out_emit, &out, ",\n  ");
        emit_comp_slot(json_out_emit, &out, "comp_slot_stage", stage);
        emit_stage_cupfull(json_out_emit, &out, "comp_slot_stage", stage,
                           ((const DIYCompetitionEx *)&_primaryCompetitionBuf)->base.numTeams);

        /* GROUP-phase stage standings. The 64-row DIYTeamStats table in the
         * stage's relocated leagueStandings[] field (DIY_STANDINGS_OFFSET) holds
         * each tournament team's group results. Its
         * int32 teamSelOffset is a DERIVED team IDENTITY (step-4 phase 3): row i
         * always resolves to selTeamsPtr team i, so the identity is simply i,
         * built by DIY_CacheSelectedTeamPointers. We do
         * NOT serialize it (it byte-swaps on LE<->BE and is fully derivable
         * from the row index). Instead serialize
         * ONLY the non-derivable stats endian-neutral and RECONSTRUCT
         * teamSelOffset = i (the identity) on load (which fixes the group team
         * NAMES that resolve via teamEntryOffsets->row->teamSelOffset->team via
         * diy_resolve_team). Indexed by
         * selTeamsPtr team index 0..primaryCompetitionBuf.tourn.header.numTeams-1 so every group's table +
         * the re-sort on group switch are correct cross-target. Gated to
         * LEAGUE/group stages (slot isLeagueFormat != 0): a KNOCKOUT stage has no
         * league table — its bracket lives separately in bracketScratch
         * (post-44e6f81 leagueStandings no longer overlays it), so leagueStandings
         * is unused/stale there and emitting these rows would just serialize
         * stale data. competitionType is 0 for both, so route off
         * the slot type. */
        if (diy_stage_is_league(stage)) {
            uint16_t nrows = _primaryCompetitionBuf.tourn.header.numTeams; if (nrows > 64) nrows = 64;
            mjson_printf(json_out_emit, &out, ",\n  %Q:[", "stage_standings");
            for (uint16_t i = 0; i < nrows; i++) {
                DIYTeamStats *r = diy_ovr_row((DIYCompetitionEx *)stage, i);
                if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
                mjson_printf(json_out_emit, &out,
                    "{%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u}",
                    "gp",  (unsigned)r->gamesPlayed,
                    "w",   (unsigned)r->wins,
                    "d",   (unsigned)r->draws,
                    "l",   (unsigned)r->losses,
                    "gf",  (unsigned)r->goalsFor,
                    "ga",  (unsigned)r->goalsAgainst,
                    "pts", (unsigned)r->points);
            }
            mjson_printf(json_out_emit, &out, "%s", "]");
        } else {
            /* CUP/knockout stage: three parallel host-native uint16 score
             * arrays (diy_competition.c:2529) — each entry is a packed score
             * that byte-swaps home/away on LE<->BE (live: GERMANY 2-0 shown
             * 0-2). Emit endian-neutral; restore host-native on load. 18
             * entries each (max matches per round). teamOffsetTable +
             * cupTeamIds/teamGridPairings are handled separately. */
            const uint16_t *st = (const uint16_t *)&stage->matchData.cup.cupRoundResults[0x40];
            const uint16_t *ag = stage->cupAggregateScores;
            const uint16_t *pn = (const uint16_t *)&stage->leagueSectionState.phaseStartMonths[0];
            const char *keys[3] = { "stage_score_tab", "stage_score_agg", "stage_score_pen" };
            const uint16_t *arrs[3] = { st, ag, pn };
            for (int a = 0; a < 3; a++) {
                mjson_printf(json_out_emit, &out, ",\n  %Q:[", keys[a]);
                for (int i = 0; i < 18; i++) {
                    if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
                    mjson_printf(json_out_emit, &out, "%u", (unsigned)arrs[a][i]);
                }
                mjson_printf(json_out_emit, &out, "%s", "]");
            }
        }
    }

    /* DIY teams array — primaryCompetitionBuf.tourn.header.numTeams TeamFileEx records at
     * &selTeamsPtr. Same record layout as the career selected_teams. */
    mjson_printf(json_out_emit, &out, ",\n  %Q:[", "diy_teams");
    for (uint16_t i = 0; i < _primaryCompetitionBuf.tourn.header.numTeams; i++) {
        if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
        emit_team_file_ex_body(json_out_emit, &out,
                               (uint8_t *)(_selectedTeams + i));
    }
    mjson_printf(json_out_emit, &out, "%s", "]\n}\n");

    if (out.oom || !out.ptr) {
        if (out.ptr) swos_io_free(out.ptr);
        return 1;
    }
    int16_t r = WriteFile(filename, (uint8_t *)out.ptr, (uint32_t)out.len);
    swos_io_free(out.ptr);
    if (r != 0) {
        ShowFsErrorByCode((uint16_t)r);
        return 1;
    }

#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
    {
        VerifyReloadFn fn = (strcmp(kind, KIND_DIY) == 0)
                              ? verify_reload_diy
                              : verify_reload_preset;
        /* Verify EXACTLY the standalone CompetitionSlotBuffer — the payload
         * load_competition_buf restores. Same-target the primary_competition_
         * context whole-buffer blob (s_career_scalars) round-trips every byte
         * verbatim; comp_slot is a cross-target override re-writing identical
         * values on top.
         *
         * For a LEAGUE preset/DIY this diffs to 0. For a CUP-stage preset it
         * shows a SMALL EXPECTED nonzero count (~12 on the WC/euro presets):
         * load_comp_slot RECONSTRUCTS the stage snapshot's derived
         * bracketScratch.teamOffsetTable from cupTeamIds (id-based — bracket
         * order != selTeamsPtr order) and does NOT re-apply the stage's
         * competition-name copy, so those snapshot bytes legitimately differ
         * from the saved raw blob. The teamOffsetTable is a DERIVED cache, not
         * authored state (see project_tournament_type2_save). These are benign;
         * any count BEYOND that small derived-snapshot set is a real regression.
         *
         * Do NOT revive the legacy [&primaryCompetitionBuf, &numSelectedTeams +
         * primaryCompetitionBuf.tourn.header.numTeams*684) span: the competition-slots untangle (2026-06-04)
         * made competitionFileBuffer a STANDALONE buffer ~1.7 MB BELOW
         * numSelectedTeams, so that span is the forbidden address-range pattern
         * over a linker GAP of unrelated globals. memset-ing the gap both
         * zeroed the reload's own dependencies (primaryCompetitionBuf.tourn.header.numTeams et al., which then
         * mis-drove the restore) and diffed ~112k unrelated bytes as false
         * mismatches. The diy-teams append (selTeamsPtr) is no longer
         * contiguous after the buffer — it round-trips via "$.diy_teams" and is
         * not part of this region. See project_diy_preset_load_memset_gap. */
        verify_roundtrip_region(kind, (uint8_t *)&_primaryCompetitionBuf,
                                (uint32_t)sizeof(CompetitionSlotBuffer),
                                fn, filename, NULL);
    }
#endif

    return 0;
}

static int16_t load_competition_buf(uint8_t *filename, const char *kind)
{
    uint8_t *src = NULL;
    uint32_t src_size = 0;
    if (read_save_file_heap(filename, &src, &src_size) != 0)
        return 1;

    long version = 0;
    if (mjson_get_long((const char *)src, (int)src_size,
                         "$.schema_version", &version) <= 0 ||
        (int)version != SAVE_JSON_SCHEMA_VERSION) {
        swos_io_free(src);
        return 1;
    }
    char file_kind[16] = {0};
    if (mjson_get_string((const char *)src, (int)src_size,
                         "$.file_kind", file_kind, sizeof(file_kind)) <= 0 ||
        strcmp(file_kind, kind) != 0 || !save_world_ok(src, src_size)) {
        swos_io_free(src);
        return 1;
    }

    /* Zero the destination FIELD-BY-FIELD (each comp-tagged s_career_scalars
     * entry by its own addr,size), NOT with one memset over a [lo,hi) span.
     * After the S6 untangle competitionFileBuffer became a STANDALONE
     * CompetitionSlotBuffer ~1.7 MB BELOW the careerFileBuffer overlay, so
     * &primaryCompetitionBuf and &numSelectedTeams no longer bracket a
     * contiguous region. A single `memset` over that span scythed the ~1.7 MB
     * linker gap, zeroing unrelated globals the linker placed there
     * (spriteBlitQueue, menuEntries, sprite tables, ...) → spriteBlitQueue==0
     * → *(uint32_t*)0 SIGSEGV in the next menu sprite draw on any DIY/preset
     * reload (live-confirmed via gdb). See project_diy_preset_load_memset_gap.
     *
     * Selecting the fields to zero by the `comp` tag (their identity) instead
     * of by address range clears EXACTLY what the comp save restores and never
     * touches the gap — and, unlike the old field_in_range(&primaryCompetitionBuf,
     * &numSelectedTeams) span, the set is build/target-independent (the span
     * inverts to empty when the two symbols sort the other way, e.g. on the
     * 32-bit Amiga). Since opaque_data is retired, every save-relevant byte is
     * covered by a typed field/blob. (The primary slot itself is the
     * primary_competition_context FLD_BYTES blob — zeroed whole here.) */
    for (const FieldDesc *f = s_career_scalars; f->type != FLD_END; f++) {
        /* comp==2 is the settings block (user tactics + options): saved and
         * restored with the competition, but NEVER zeroed here. A file written
         * before those keys existed has nothing to restore, and zeroing would
         * leave the six USER tactic slots blank instead of untouched. */
        if (f->comp != 1) continue;
        uint32_t sz = field_byte_size(f);
        if (sz) memset(f->addr, 0, sz);
    }

    /* Reset string pool — see comment in apply_semantic_load. */
    string_pool_reset();

    /* primaryCompStringOffset is left at 0 from the memset above —
     * InitializeNewSeason tolerates a NULL value here by writing a
     * NULL into the season-end summary's slot, which prints as an
     * empty competition name. The legacy emit was a stale absolute
     * pointer that would have crashed if dereferenced after a
     * cross-build load anyway. */

    load_scalar_table_comp((const char *)src, (int)src_size,
                           "comp_scalars", s_career_scalars);

    /* Typed primary competition slot — applied AFTER the scalar/blob load
     * so it re-lays the cross-target-sensitive header + team-id + cup +
     * TOURNAMENT-slot fields struct-anchored / endian-neutral (see emit).
     * Absent in pre-serializer .PRE/.DIY saves → load_comp_slot is a
     * no-op (mjson_find returns non-object), so older saves still load. */
    load_comp_slot((const char *)src, (int)src_size, "comp_slot",
                   (DIYCompetitionEx *)&_primaryCompetitionBuf);

    /* Cross-target league-standings override — apply AFTER load_comp_slot/blob
     * so it corrects the struct-correct positions (same as the career path's
     * apply_semantic_load). Re-derives each row's teamSelOffset from the saved
     * team identity, fixing the LE<->BE int32 byte-swap that otherwise garbles
     * a .DIY/.PRE LEAGUE table cross-target. type != 0 (cup/tournament) → no-op;
     * pre-fix saves lack the key → no-op (blob bytes stand, same-target-fine). */
    load_competition_overrides((const char *)src, (int)src_size);

    /* TYPE-2 tournament current-STAGE slot (see write_competition_buf). The
     * master load above sets competitionFileBuffer's type; if it's a
     * tournament, re-lay the stage DIYCompetitionEx in stageSnapshot typed/endian-
     * neutral over the raw blob, BEFORE the game's diy_cfb_to_tournament
     * copies the stageSnapshot -> diyCompetitionStart (so the corrected bytes are
     * what gets copied). Absent in pre-fix saves -> mjson_find misses ->
     * load_comp_slot is a no-op, blob bytes stand. */
    if (((const DIYCompetitionEx *)&_primaryCompetitionBuf)
            ->base.competitionType == 2) {
        DIYCompetitionEx *stage = (DIYCompetitionEx *)
            ((uint8_t *)&_primaryCompetitionBuf + offsetof(CompetitionSlotBuffer, stageSnapshot));
        load_comp_slot((const char *)src, (int)src_size, "comp_slot_stage",
                       stage);
        load_stage_cupfull((const char *)src, (int)src_size, "comp_slot_stage", stage,
                           ((const DIYCompetitionEx *)&_primaryCompetitionBuf)->base.numTeams);

        /* Rebuild the GROUP-phase standings (see write_competition_buf):
         * RECONSTRUCT each row's derived teamSelOffset = i (the team identity;
         * row i = selTeamsPtr team i — the cache DIY_CacheSelectedTeamPointers
         * builds) and restore the endian-neutral stats. LEAGUE/group stages
         * only — a KNOCKOUT stage has no league table; its bracket lives in
         * bracketScratch and is rebuilt separately after the diy_teams load
         * below (see the cup teamOffsetTable reconstruct). */
        if (diy_stage_is_league(stage)) {
            const char *rows = NULL; int rows_len = 0;
            if (mjson_find((const char *)src, (int)src_size,
                           "$.stage_standings", &rows, &rows_len)
                    == MJSON_TOK_ARRAY) {
                int pos = 0, voff, vlen, vt; uint16_t i = 0;
                while (i < _primaryCompetitionBuf.tourn.header.numTeams && i < 64 &&
                       (pos = fast_array_next(rows, rows_len, pos,
                                              &voff, &vlen, &vt)) != 0) {
                    if (vt != MJSON_TOK_OBJECT) { i++; continue; }
                    const char *e = rows + voff; int el = vlen;
                    long gp = 0, w = 0, d = 0, l = 0, gf = 0, ga = 0, pts = 0;
                    mjson_get_long(e, el, "$.gp",  &gp);
                    mjson_get_long(e, el, "$.w",   &w);
                    mjson_get_long(e, el, "$.d",   &d);
                    mjson_get_long(e, el, "$.l",   &l);
                    mjson_get_long(e, el, "$.gf",  &gf);
                    mjson_get_long(e, el, "$.ga",  &ga);
                    mjson_get_long(e, el, "$.pts", &pts);
                    DIYTeamStats *r = diy_ovr_row(stage, i);
                    r->teamSelOffset = (int32_t)i; /* identity = selTeamsPtr team index i (step-4 phase 3) */
                    r->gamesPlayed  = (uint16_t)gp;
                    r->wins         = (uint16_t)w;
                    r->draws        = (uint16_t)d;
                    r->losses       = (uint16_t)l;
                    r->goalsFor     = (uint16_t)gf;
                    r->goalsAgainst = (uint16_t)ga;
                    r->points       = (uint16_t)pts;
                    i++;
                }
            }
        } else {
            /* CUP/knockout stage scores (see write_competition_buf): restore
             * the three host-native uint16 arrays from the endian-neutral
             * JSON, fixing the home/away score swap cross-target. */
            uint16_t *dsts[3] = {
                (uint16_t *)&stage->matchData.cup.cupRoundResults[0x40],
                stage->cupAggregateScores,
                (uint16_t *)&stage->leagueSectionState.phaseStartMonths[0],
            };
            const char *keys[3] = { "$.stage_score_tab", "$.stage_score_agg",
                                    "$.stage_score_pen" };
            for (int a = 0; a < 3; a++) {
                const char *arr = NULL; int alen = 0;
                if (mjson_find((const char *)src, (int)src_size, keys[a],
                               &arr, &alen) != MJSON_TOK_ARRAY) continue;
                int pos = 0, voff, vlen, vt, i = 0;
                while (i < 18 &&
                       (pos = fast_array_next(arr, alen, pos,
                                              &voff, &vlen, &vt)) != 0) {
                    if (vt == MJSON_TOK_NUMBER)
                        dsts[a][i] = (uint16_t)parse_long_slice(arr + voff, vlen);
                    i++;
                }
            }
        }
    }

    /* DIY teams — walk array via mjson_next so each iteration is
     * O(1) in array position. */
    const char *teams_arr = NULL;
    int teams_arr_len = 0;
    if (mjson_find((const char *)src, (int)src_size, "$.diy_teams",
                   &teams_arr, &teams_arr_len) == MJSON_TOK_ARRAY) {
        int pos = 0, koff, klen, voff, vlen, vt;
        uint16_t i = 0;
        while (i < _primaryCompetitionBuf.tourn.header.numTeams &&
               (pos = mjson_next(teams_arr, teams_arr_len, pos,
                                 &koff, &klen, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) { i++; continue; }
            load_team_file_ex_slice(teams_arr + voff, vlen,
                                    (uint8_t *)(_selectedTeams + i));
            i++;
        }
    }

    /* numSelectedTeams: set from the construction invariant. For a standalone
     * .DIY/.PRE the selected teams ARE the diy teams, so numSelectedTeams ==
     * primaryCompetitionBuf.tourn.header.numTeams by construction. The saved
     * num_selected_teams_inner is now comp-tagged and IS reliably applied by
     * load_scalar_table_comp on both targets (it no longer depends on a runtime
     * [&primaryCompetitionBuf, &numSelectedTeams) span — which inverted to empty
     * on the 32-bit Amiga, silently dropping the field → 0 → findTeamInCache
     * bails → cup VerifyTeamLoaded panic). We still re-derive from numTeams here
     * as the canonical, target-independent value. */
    _numSelectedTeams = _primaryCompetitionBuf.tourn.header.numTeams;

    /* TYPE-2 tournament KNOCKOUT stage: rebuild the cup bracket
     * teamOffsetTable from cupTeamIds. teamOffsetTable[k] is a DERIVED
     * pointer cache (= the selTeamsPtr byte-offset of cupTeamIds[k]'s team,
     * built by DIY_BuildTeamSelectionPointerCache); it byte-swaps on
     * LE<->BE, and the preset-load routes through the league branch (stage
     * header competitionType==0) so it's never rebuilt -> garbage R16 team
     * NAMES (which resolve teamGridPairings -> teamOffsetTable ->
     * &selTeamsPtr -> teamName). cupTeamIds (BE byte-pairs) + teamGridPairings
     * (bytes) ride the raw blob endian-safe, so resolve each team via
     * VerifyTeamLoaded (the same lookup the cache builder uses) now that
     * selTeamsPtr (diy_teams) is loaded. cupTeamIds[0..primaryCompetitionBuf.tourn.header.numTeams-1] are the
     * tournament teams (kept below the standings overlay at index 51).
     * LEAGUE/group stages skip this — their standings were rebuilt above. */
    if (((const DIYCompetitionEx *)&_primaryCompetitionBuf)
            ->base.competitionType == 2) {
        DIYCompetitionEx *stage = (DIYCompetitionEx *)
            ((uint8_t *)&_primaryCompetitionBuf + offsetof(CompetitionSlotBuffer, stageSnapshot));
        if (!diy_stage_is_league(stage)) {
            uint16_t n = _primaryCompetitionBuf.tourn.header.numTeams; if (n > 51) n = 51;
            for (uint16_t k = 0; k < n; k++) {
                uint16_t teamNum = BE_READ_U16(&stage->cupTeamIds[k]);
                if (teamNum == 0) continue;
                TeamFileEx *tp = VerifyTeamLoaded(teamNum);
                stage->bracketScratch.teamOffsetTable[k] =
                    diy_team_to_identity((uint8_t *)tp);  /* identity: -1 careerTeam / index */
            }
        }
    }

    /* NB: do NOT call RebuildPointersAfterLoad() here. It is the CAREER
     * post-load fixup (re-derives currentCompetitionPtr et al. from
     * competitionsTable + selTeamNumber) and is wrong for a standalone
     * DIY/preset — a correctly-routed .DIY/.PRE goes to the DIY menu
     * (diyLeagueTournamentMenu / ShowDIYCupMenuWithParams), which reads only
     * diyCompetitionStart/competitionFileBuffer and is set up by StartContest's
     * own DIY path. The earlier crash was a SYMPTOM of mis-routing to the career
     * contestMenu because gameType wasn't restored; the real fix is setting
     * gameType in LoadDIYFile/LoadPresetFile. See project_diy_preset_load_gametype. */
    swos_io_free(src);
    return 0;
}

int16_t SaveJson_WritePreset(uint8_t *filename)
{
    return write_competition_buf(filename, KIND_PRESET);
}

int16_t SaveJson_LoadPreset(uint8_t *filename, const uint8_t *src_unused,
                            uint32_t src_size_unused)
{
    (void)src_unused; (void)src_size_unused;
    return load_competition_buf(filename, KIND_PRESET);
}

int16_t SaveJson_WriteDIY(uint8_t *filename)
{
    return write_competition_buf(filename, KIND_DIY);
}

int16_t SaveJson_LoadDIY(uint8_t *filename, const uint8_t *src_unused,
                         uint32_t src_size_unused)
{
    (void)src_unused; (void)src_size_unused;
    return load_competition_buf(filename, KIND_DIY);
}

/* ---------- .HIL (highlights / match replay) ----------
 *
 * Fully semantic encoding (no opaque blobs). On-disk layout:
 *   +0      sig          u32 BE
 *   +4      max_size     u32 BE
 *   +8      topTeamInGame    1018-byte TeamGame (BE int16 header)
 *   +1026   bottomTeamInGame 1018-byte TeamGame
 *   +2044   gameName[100]
 *   +2144   gameRound[100]
 *   +2244   numGoals          u16 (BE on disk, host-native at our
 *                                  dispatch point — DoSaveHighlights
 *                                  hasn't applied its swap yet)
 *   +2246   team1Goals        u16
 *   +2248   team2Goals        u16
 *   +2250   pitchNumber       u16
 *   +2252   _maxSubstitutes    u16
 *   +2254   scenes[numGoals]  each scene = 19000-byte ring of frames
 *
 * Scene format (per SaveCoordinatesForHighlights / DrawReplayFrame):
 *   - Sequence of u32-aligned entries, BE on disk.
 *   - bit 31 set = frame marker:
 *       [marker u32 = 0x80000000 | (cam_x << 16) | cam_y]
 *       [u32 = (team1_goals << 16) | team2_goals]
 *       [u32 = (anim_state << 16) | anim_state]  // duplicated
 *   - bit 31 clear = sprite entry:
 *       [u32 = (sprite_index << 20) | (x << 10) | y]
 *   - 0xFFFFFFFF = end-of-segment sentinel; anything after up to
 *     19000 bytes is garbage from the ring buffer (preserved here
 *     as a "trailing" byte array so round-trip is bit-exact).
 */

extern uint8_t  _hilFileBuffer[];
extern uint16_t _hilNumGoals;
extern int      DosGameplay_TargetFps(void);   /* 70 DOS / 50 Amiga — stored as replay_fps */
extern uint8_t  _hilGameName[];
extern uint8_t  _hilGameRound[];

/* On-disk .HIL header — 2254 bytes total. Matches the byte layout
 * the legacy save/load and SetHilFileHeader / SaveCoordinatesForHighlights
 * write at the start of hilFileBuffer. Using offsetof() against this
 * struct replaces every hardcoded byte offset that used to live in
 * this file. Note: signature/max_size/TeamGame kit fields are BE on
 * disk; numGoals + neighbours are host-native at our save dispatch
 * point (the legacy BE swap dance hasn't run yet). */
typedef struct __attribute__((packed)) HilFileHeader {
    uint32_t  signature;          /* +0   BE on disk */
    uint32_t  max_size;           /* +4   BE on disk */
    TeamGame  top_team;           /* +8   1018 bytes (BE int16 header) */
    TeamGame  bottom_team;        /* +1026 1018 bytes */
    uint8_t   game_name[100];     /* +2044 */
    uint8_t   game_round[100];    /* +2144 */
    uint16_t  num_goals;          /* +2244 */
    uint16_t  team_1_goals;       /* +2246 */
    uint16_t  team_2_goals;       /* +2248 */
    uint16_t  pitch_number;       /* +2250 */
    uint16_t  max_substitutes;    /* +2252 */
} HilFileHeader;

typedef char _hilheader_size_check[(sizeof(HilFileHeader) == 2254) ? 1 : -1];

#define HIL_HEADER_SIZE  ((uint32_t)sizeof(HilFileHeader))
#define HIL_SCENE_SIZE   19000u

/* ---------- TeamGame emit/load ----------
 *
 * In hilFileBuffer (memory and on disk), the 11 × int16 fields are
 * BE-encoded by SetHilFileHeader. We read via BE_READ_U16 to get
 * the live host-native values, emit as JSON numbers, and on load
 * write back via HOST_TO_BE16.
 */
static void emit_team_game(mjson_print_fn_t fn, void *fnd,
                           const char *key, const uint8_t *tg)
{
    mjson_printf(fn, fnd, "%Q:{", key);

    mjson_printf(fn, fnd,
        "%Q:{"
        "%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,"
        "%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u},",
        "kit",
        "pr_shirt_type",  (unsigned)BE_READ_U16(tg + offsetof(TeamGame, prShirtType)),
        "pr_shirt_col",   (unsigned)BE_READ_U16(tg + offsetof(TeamGame, prShirtCol)),
        "pr_stripes_col", (unsigned)BE_READ_U16(tg + offsetof(TeamGame, prStripesCol)),
        "pr_shorts_col",  (unsigned)BE_READ_U16(tg + offsetof(TeamGame, prShortsCol)),
        "pr_socks_col",   (unsigned)BE_READ_U16(tg + offsetof(TeamGame, prSocksCol)),
        "sec_shirt_type", (unsigned)BE_READ_U16(tg + offsetof(TeamGame, secShirtType)),
        "sec_shirt_col",  (unsigned)BE_READ_U16(tg + offsetof(TeamGame, secShirtCol)),
        "sec_stripes",    (unsigned)BE_READ_U16(tg + offsetof(TeamGame, secStripesCol)),
        "sec_shorts",     (unsigned)BE_READ_U16(tg + offsetof(TeamGame, secShortsCol)),
        "sec_socks",      (unsigned)BE_READ_U16(tg + offsetof(TeamGame, secSocksCol)));

    mjson_printf(fn, fnd, "%Q:%u,", "marked_player",
                 (unsigned)BE_READ_U16(tg + offsetof(TeamGame, markedPlayer)));
    emit_str_at(fn, fnd, "team_name", (uint8_t *)tg, offsetof(TeamGame, teamName), 17);
    mjson_printf(fn, fnd, ",%Q:%u,%Q:%u,%Q:%u",
                 "reserved1",     (unsigned)tg[offsetof(TeamGame, reserved1)],
                 "num_own_goals", (unsigned)tg[offsetof(TeamGame, numOwnGoals)],
                 "reserved3",     (unsigned)tg[offsetof(TeamGame, reserved3)]);

    mjson_printf(fn, fnd, ",%Q:[", "players");
    for (int i = 0; i < 16; i++) {
        if (i > 0) mjson_printf(fn, fnd, "%s", ",");
        const uint8_t *p = tg + offsetof(TeamGame, players)
                              + (size_t)i * sizeof(struct PlayerInfo);
        mjson_printf(fn, fnd,
            "{%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
            "%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,",
            "skip",          (int)(int8_t)p[offsetof(struct PlayerInfo, skip)],
            "player_index",  (int)(int8_t)p[offsetof(struct PlayerInfo, playerIndex)],
            "goals_scored",  (int)(int8_t)p[offsetof(struct PlayerInfo, goalsScored)],
            "shirt_number",  (int)(int8_t)p[offsetof(struct PlayerInfo, shirtNumber)],
            "position",      (int)(int8_t)p[offsetof(struct PlayerInfo, position)],
            "face",          (int)(int8_t)p[offsetof(struct PlayerInfo, face)],
            "is_injured",    (int)(int8_t)p[offsetof(struct PlayerInfo, isInjured)],
            "field_7",       (int)(int8_t)p[offsetof(struct PlayerInfo, field_7)],
            "field_8",       (int)(int8_t)p[offsetof(struct PlayerInfo, field_8)],
            "previous_cards",(int)(int8_t)p[offsetof(struct PlayerInfo, previousCards)],
            "num_yellow_cards",(int)(int8_t)p[offsetof(struct PlayerInfo, numYellowCards)],
            "field_b",       (int)(int8_t)p[offsetof(struct PlayerInfo, field_B)]);
        emit_str_at(fn, fnd, "short_name", (uint8_t *)p,
                    offsetof(struct PlayerInfo, shortName), 15);
        mjson_printf(fn, fnd,
            ",%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,%Q:%d,"
            "%Q:%d,%Q:%d,%Q:%d,%Q:%d,",
            "passing",       (int)(int8_t)p[offsetof(struct PlayerInfo, passing)],
            "shooting",      (int)(int8_t)p[offsetof(struct PlayerInfo, shooting)],
            "heading",       (int)(int8_t)p[offsetof(struct PlayerInfo, heading)],
            "tackling",      (int)(int8_t)p[offsetof(struct PlayerInfo, tackling)],
            "ball_control",  (int)(int8_t)p[offsetof(struct PlayerInfo, ballControl)],
            "speed",         (int)(int8_t)p[offsetof(struct PlayerInfo, speed)],
            "finishing",     (int)(int8_t)p[offsetof(struct PlayerInfo, finishing)],
            "goalie_skill",  (int)(int8_t)p[offsetof(struct PlayerInfo, goalieSkill)],
            "injuries_bits", (int)(int8_t)p[offsetof(struct PlayerInfo, injuriesBitfield)],
            "half_played",   (int)(int8_t)p[offsetof(struct PlayerInfo, halfPlayed)],
            "face_2",        (int)(int8_t)p[offsetof(struct PlayerInfo, face2)]);
        emit_str_at(fn, fnd, "full_name", (uint8_t *)p,
                    offsetof(struct PlayerInfo, fullName), 23);
        mjson_printf(fn, fnd, "%s", "}");
    }
    mjson_printf(fn, fnd, "%s", "]}");
}

/* Sliced variant — caller has already located the TeamGame's JSON
 * object slice via mjson_find. Subsequent get_* lookups scan only
 * the ~10 KB slice instead of the entire .HIL JSON. */
static void load_team_game_slice(const char *buf, int buflen, uint8_t *tg)
{
    long num = 0.0;
    char path[40];

    /* Zero the buffer up front — fields not present in JSON stay
     * defined as zero. */
    memset(tg, 0, sizeof(TeamGame));

    /* Kit fields live under "$.kit" — slice the sub-object so the
     * 10 lookups don't scan the whole TeamGame slice. */
    const char *kit = NULL; int kit_len = 0;
    if (mjson_find(buf, buflen, "$.kit",
                   &kit, &kit_len) == MJSON_TOK_OBJECT) {
#define LOAD_BE16(key, field) do { \
            if (mjson_get_long(kit, kit_len, "$." key, &num) > 0) \
                BE_WRITE_U16(tg + offsetof(TeamGame, field), (uint16_t)(int)num); \
        } while (0)
        LOAD_BE16("pr_shirt_type",  prShirtType);
        LOAD_BE16("pr_shirt_col",   prShirtCol);
        LOAD_BE16("pr_stripes_col", prStripesCol);
        LOAD_BE16("pr_shorts_col",  prShortsCol);
        LOAD_BE16("pr_socks_col",   prSocksCol);
        LOAD_BE16("sec_shirt_type", secShirtType);
        LOAD_BE16("sec_shirt_col",  secShirtCol);
        LOAD_BE16("sec_stripes",    secStripesCol);
        LOAD_BE16("sec_shorts",     secShortsCol);
        LOAD_BE16("sec_socks",      secSocksCol);
#undef LOAD_BE16
    }

    if (mjson_get_long(buf, buflen, "$.marked_player", &num) > 0)
        BE_WRITE_U16(tg + offsetof(TeamGame, markedPlayer), (uint16_t)(int)num);

    load_str_at(buf, buflen, "$.team_name", tg,
                offsetof(TeamGame, teamName), 17);

#define LOAD_B(key, field) do { \
        if (mjson_get_long(buf, buflen, "$." key, &num) > 0) \
            tg[offsetof(TeamGame, field)] = (uint8_t)(int)num; \
    } while (0)
    LOAD_B("reserved1",     reserved1);
    LOAD_B("num_own_goals", numOwnGoals);
    LOAD_B("reserved3",     reserved3);
#undef LOAD_B

    /* Walk the players array via mjson_next — same O(N) pattern
     * as elsewhere. */
    const char *players = NULL; int players_len = 0;
    if (mjson_find(buf, buflen, "$.players",
                   &players, &players_len) != MJSON_TOK_ARRAY)
        return;

    int pos = 0, koff, klen, voff, vlen, vt;
    int i = 0;
    while (i < 16 &&
           (pos = mjson_next(players, players_len, pos,
                             &koff, &klen, &voff, &vlen, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) { i++; continue; }
        uint8_t *p = tg + offsetof(TeamGame, players)
                        + (size_t)i * sizeof(struct PlayerInfo);
        const char *ps = players + voff;
        int psl = vlen;
        i++;

#define LOAD_PB(key, field) do { \
            if (mjson_get_long(ps, psl, "$." key, &num) > 0) \
                p[offsetof(struct PlayerInfo, field)] = (uint8_t)(int)(int8_t)(int)num; \
        } while (0)
        LOAD_PB("skip",            skip);
        LOAD_PB("player_index",    playerIndex);
        LOAD_PB("goals_scored",    goalsScored);
        LOAD_PB("shirt_number",    shirtNumber);
        LOAD_PB("position",        position);
        LOAD_PB("face",            face);
        LOAD_PB("is_injured",      isInjured);
        LOAD_PB("field_7",         field_7);
        LOAD_PB("field_8",         field_8);
        LOAD_PB("previous_cards",  previousCards);
        LOAD_PB("num_yellow_cards",numYellowCards);
        LOAD_PB("field_b",         field_B);
        load_str_at(ps, psl, "$.short_name", p,
                    offsetof(struct PlayerInfo, shortName), 15);
        LOAD_PB("passing",         passing);
        LOAD_PB("shooting",        shooting);
        LOAD_PB("heading",         heading);
        LOAD_PB("tackling",        tackling);
        LOAD_PB("ball_control",    ballControl);
        LOAD_PB("speed",           speed);
        LOAD_PB("finishing",       finishing);
        LOAD_PB("goalie_skill",    goalieSkill);
        LOAD_PB("injuries_bits",   injuriesBitfield);
        LOAD_PB("half_played",     halfPlayed);
        LOAD_PB("face_2",          face2);
        load_str_at(ps, psl, "$.full_name", p,
                    offsetof(struct PlayerInfo, fullName), 23);
#undef LOAD_PB
    }
}

/* Path-based wrapper — locates the slice then delegates. */
static void load_team_game(const char *buf, int buflen,
                           const char *path_prefix, uint8_t *tg)
{
    const char *slice = NULL; int slice_len = 0;
    if (mjson_find(buf, buflen, path_prefix,
                   &slice, &slice_len) == MJSON_TOK_OBJECT)
        load_team_game_slice(slice, slice_len, tg);
    else
        memset(tg, 0, sizeof(TeamGame));
}

/* ---------- scene emit/load ----------
 *
 * A scene is 19000 bytes of 4-byte BE entries. Parse into a list of
 * frames + sprites; emit any bytes after the end-of-segment sentinel
 * as a trailing byte array so we round-trip bit-exactly. */
static void emit_scene(mjson_print_fn_t fn, void *fnd,
                       const uint8_t *scene)
{
    mjson_printf(fn, fnd, "%s", "{\"frames\":[");

    uint32_t off = 0;
    int frame_open = 0;
    int frame_first_sprite = 1;
    int frames_emitted = 0;
    uint32_t eos_off = HIL_SCENE_SIZE;

    while (off + 4 <= HIL_SCENE_SIZE) {
        uint32_t entry = BE_READ_U32(scene + off);
        if (entry == 0xFFFFFFFFu) {
            eos_off = off + 4;
            break;
        }
        if (entry & 0x80000000u) {
            /* Close previous frame */
            if (frame_open) {
                mjson_printf(fn, fnd, "%s", "]}");
            }

            /* New frame: read 3 × u32. camera x|0x8000, camera y, goals u16+u16, anim u16+u16 (dup). */
            uint16_t cam_x = (entry >> 16) & 0x7FFFu;  /* drop bit 31 */
            uint16_t cam_y = entry & 0xFFFFu;
            uint32_t goals  = (off + 8  <= HIL_SCENE_SIZE) ? BE_READ_U32(scene + off + 4) : 0;
            uint32_t anim   = (off + 12 <= HIL_SCENE_SIZE) ? BE_READ_U32(scene + off + 8) : 0;
            uint16_t team1_goals = (uint16_t)(goals >> 16);
            uint16_t team2_goals = (uint16_t)(goals & 0xFFFF);
            uint16_t anim_state  = (uint16_t)(anim >> 16);  /* low half duplicates high */

            if (frames_emitted > 0) mjson_printf(fn, fnd, "%s", ",");
            mjson_printf(fn, fnd,
                "{%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:[",
                "cam_x", (unsigned)cam_x,
                "cam_y", (unsigned)cam_y,
                "team_1_goals", (unsigned)team1_goals,
                "team_2_goals", (unsigned)team2_goals,
                "anim_patterns_state", (unsigned)anim_state,
                "sprites");
            frame_open = 1;
            frame_first_sprite = 1;
            frames_emitted++;
            off += 12;
        } else {
            /* Sprite entry: bit 31 clear, encoded (idx << 20) | (x << 10) | y. */
            uint32_t idx = (entry >> 20) & 0xFFFu;
            uint32_t x   = (entry >> 10) & 0x3FFu;
            uint32_t y   = entry & 0x3FFu;
            if (!frame_open) {
                /* Sprite without a preceding frame — preserve as a
                 * leading-sprites array on a synthetic frame. Rare but
                 * possible if the scene starts mid-frame. */
                if (frames_emitted > 0) mjson_printf(fn, fnd, "%s", ",");
                mjson_printf(fn, fnd, "%s",
                    "{\"cam_x\":0,\"cam_y\":0,\"team_1_goals\":0,"
                    "\"team_2_goals\":0,\"anim_patterns_state\":0,"
                    "\"_synthetic_no_frame_marker\":true,\"sprites\":[");
                frame_open = 1;
                frame_first_sprite = 1;
                frames_emitted++;
            }
            if (!frame_first_sprite) mjson_printf(fn, fnd, "%s", ",");
            frame_first_sprite = 0;
            mjson_printf(fn, fnd, "{%Q:%u,%Q:%u,%Q:%u}",
                         "index", (unsigned)idx,
                         "x",     (unsigned)x,
                         "y",     (unsigned)y);
            off += 4;
        }
    }

    if (frame_open) {
        mjson_printf(fn, fnd, "%s", "]}");
    }
    mjson_printf(fn, fnd, "%s", "]");

    /* Trailing bytes: everything from eos_off..HIL_SCENE_SIZE (the
     * ring-buffer garbage past the active region). Emit as a byte
     * array for bit-exact round-trip. */
    mjson_printf(fn, fnd, ",%Q:%u,%Q:[",
                 "eos_offset", (unsigned)eos_off, "trailing");
    for (uint32_t i = eos_off; i < HIL_SCENE_SIZE; i++) {
        if (i > eos_off) mjson_printf(fn, fnd, "%s", ",");
        mjson_printf(fn, fnd, "%u", (unsigned)scene[i]);
    }
    mjson_printf(fn, fnd, "%s", "]}");
}

/* Forward decl — caller-provides slice variant. */
static void load_scene_slice(const char *scene_buf, int scene_buflen,
                             uint8_t *scene);

static void load_scene(const char *buf, int buflen,
                       const char *path_prefix, uint8_t *scene)
{
    /* Path wrapper — locate slice then delegate. */
    const char *scene_buf = NULL;
    int scene_buflen = 0;
    if (mjson_find(buf, buflen, path_prefix,
                   &scene_buf, &scene_buflen) == MJSON_TOK_OBJECT)
        load_scene_slice(scene_buf, scene_buflen, scene);
    else
        memset(scene, 0, HIL_SCENE_SIZE);
}

static void load_scene_slice(const char *scene_buf, int scene_buflen,
                             uint8_t *scene)
{
    memset(scene, 0, HIL_SCENE_SIZE);

    uint32_t off = 0;
    long num = 0.0;

#if MJSON_ENABLE_NEXT
    const char *frames_arr = NULL;
    int frames_arr_len = 0;
    if (mjson_find(scene_buf, scene_buflen, "$.frames",
                   &frames_arr, &frames_arr_len) == MJSON_TOK_ARRAY) {
        int koff, klen, voff, vlen, vt;
        int pos = 0;
        while ((pos = mjson_next(frames_arr, frames_arr_len, pos,
                                 &koff, &klen, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;

            /* mjson_next yields the frame's slice directly — use it
             * as the buffer for the field/sprite lookups. Cuts the
             * scene load from O(F²·S²) to O(F·S). */
            const char *fb = frames_arr + voff;
            int fl = vlen;

            uint16_t cam_x = 0, cam_y = 0, t1 = 0, t2 = 0, anim = 0;
            int is_synthetic = 0;

            if (mjson_get_long(fb, fl, "$.cam_x",          &num) > 0) cam_x = (uint16_t)num;
            if (mjson_get_long(fb, fl, "$.cam_y",          &num) > 0) cam_y = (uint16_t)num;
            if (mjson_get_long(fb, fl, "$.team_1_goals",   &num) > 0) t1   = (uint16_t)num;
            if (mjson_get_long(fb, fl, "$.team_2_goals",   &num) > 0) t2   = (uint16_t)num;
            if (mjson_get_long(fb, fl, "$.anim_patterns_state", &num) > 0) anim = (uint16_t)num;
            {
                int bval = 0;
                if (mjson_get_bool(fb, fl, "$._synthetic_no_frame_marker",
                                   &bval) > 0 && bval)
                    is_synthetic = 1;
            }

            if (!is_synthetic) {
                if (off + 12 > HIL_SCENE_SIZE) break;
                uint32_t marker = 0x80000000u
                                | ((uint32_t)cam_x << 16)
                                | (uint32_t)cam_y;
                BE_WRITE_U32(scene + off, marker);
                BE_WRITE_U32(scene + off + 4,
                             ((uint32_t)t1 << 16) | (uint32_t)t2);
                BE_WRITE_U32(scene + off + 8,
                             ((uint32_t)anim << 16) | (uint32_t)anim);
                off += 12;
            }

            /* Sprites — iterate the array via mjson_next instead of
             * indexed path lookups. Each sprite decode is then O(1)
             * in array position, not O(idx²). */
            const char *sprites_arr = NULL;
            int sprites_arr_len = 0;
            if (mjson_find(fb, fl, "$.sprites",
                           &sprites_arr, &sprites_arr_len) == MJSON_TOK_ARRAY) {
                int spos = 0;
                int s_koff, s_klen, s_voff, s_vlen, s_vt;
                while ((spos = mjson_next(sprites_arr, sprites_arr_len, spos,
                                          &s_koff, &s_klen, &s_voff, &s_vlen, &s_vt)) != 0) {
                    if (s_vt != MJSON_TOK_OBJECT) continue;
                    const char *sb = sprites_arr + s_voff;
                    int sl = s_vlen;

                    uint32_t s_idx = 0, s_x = 0, s_y = 0;
                    if (mjson_get_long(sb, sl, "$.index", &num) > 0)
                        s_idx = (uint32_t)num & 0xFFFu;
                    if (mjson_get_long(sb, sl, "$.x", &num) > 0)
                        s_x = (uint32_t)num & 0x3FFu;
                    if (mjson_get_long(sb, sl, "$.y", &num) > 0)
                        s_y = (uint32_t)num & 0x3FFu;
                    if (off + 4 > HIL_SCENE_SIZE) goto sprites_done;
                    uint32_t packed = (s_idx << 20) | (s_x << 10) | s_y;
                    BE_WRITE_U32(scene + off, packed);
                    off += 4;
                }
            sprites_done:;
            }
        }
    }
#else
    (void)buf; (void)buflen; (void)path_prefix;
#endif

    /* EOS sentinel */
    uint32_t eos_off = off;
    if (mjson_get_long(scene_buf, scene_buflen, "$.eos_offset", &num) > 0) {
        uint32_t hinted = (uint32_t)num;
        if (hinted >= 4 && hinted <= HIL_SCENE_SIZE)
            eos_off = hinted;
    }
    if (eos_off >= 4 && eos_off <= HIL_SCENE_SIZE) {
        BE_WRITE_U32(scene + eos_off - 4, 0xFFFFFFFFu);
    }

    /* Trailing bytes (post-EOS garbage) */
    load_bytes_at(scene_buf, scene_buflen, "$.trailing",
                  scene, eos_off, HIL_SCENE_SIZE - eos_off);
}

int16_t SaveJson_WriteHighlights(uint8_t *filename)
{
    PROF_START(total);
    uint16_t num_goals = _hilNumGoals;
    uint32_t data_size = HIL_HEADER_SIZE + (uint32_t)num_goals * HIL_SCENE_SIZE;

    JsonOutBuf out = { NULL, 0, 0, 0 };
    string_pool_reset();

    PROF_START(header);
    mjson_printf(json_out_emit, &out,
        "{\n  %Q:%d,\n  %Q:%Q,\n  ",
        "schema_version", SAVE_JSON_SCHEMA_VERSION,
        "file_kind",      KIND_HIGHLIGHTS);

    mjson_printf(json_out_emit, &out, "%Q:%lu,\n  %Q:%lu,\n  ",
                 "signature",
                 (long)(uint32_t)BE_READ_U32((uint8_t *)_hilFileBuffer
                                             + offsetof(HilFileHeader, signature)),
                 "max_size",
                 (long)(uint32_t)BE_READ_U32((uint8_t *)_hilFileBuffer
                                             + offsetof(HilFileHeader, max_size)));

    HilFileHeader *hdr = (HilFileHeader *)_hilFileBuffer;

    emit_team_game(json_out_emit, &out, "top_team",
                   (uint8_t *)&hdr->top_team);
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_team_game(json_out_emit, &out, "bottom_team",
                   (uint8_t *)&hdr->bottom_team);

    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_str_at(json_out_emit, &out, "game_name",
                (uint8_t *)_hilFileBuffer,
                offsetof(HilFileHeader, game_name),
                sizeof(hdr->game_name));
    mjson_printf(json_out_emit, &out, "%s", ",\n  ");
    emit_str_at(json_out_emit, &out, "game_round",
                (uint8_t *)_hilFileBuffer,
                offsetof(HilFileHeader, game_round),
                sizeof(hdr->game_round));

    /* `replay_fps` is the rate the match was PLAYED at, which is the rate the
     * scenes below must be played back at: 70 under DOS gameplay, 50 under the
     * Amiga's. Nothing in the old header recorded it, so file_io.c's default of
     * 50 was used for every .HIL -- and a DOS-mode recording therefore played at
     * 50/70 = 71% speed, reported as "too slow and it stutters" (ReportBot 0008).
     * A file without the field still reads as 50, which is what the Amiga
     * recordings it applies to actually are. */
    mjson_printf(json_out_emit, &out,
        ",\n  %Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u,%Q:%u",
        "num_goals",       (unsigned)num_goals,
        "team_1_goals",    (unsigned)hdr->team_1_goals,
        "team_2_goals",    (unsigned)hdr->team_2_goals,
        "pitch_number",    (unsigned)hdr->pitch_number,
        "max_substitutes", (unsigned)hdr->max_substitutes,
        "replay_fps",      (unsigned)DosGameplay_TargetFps());
    PROF_END(header, "highlights-write");

    PROF_START(scenes);
    mjson_printf(json_out_emit, &out, ",\n  %Q:[", "scenes");
    for (uint16_t i = 0; i < num_goals; i++) {
        if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
        emit_scene(json_out_emit, &out,
                   _hilFileBuffer + HIL_HEADER_SIZE
                                 + (uint32_t)i * HIL_SCENE_SIZE);
    }
    mjson_printf(json_out_emit, &out, "%s", "]\n}\n");
    PROF_END(scenes, "highlights-write");

    if (out.oom || !out.ptr) {
        if (out.ptr) swos_io_free(out.ptr);
        return 1;
    }

    PROF_START(write_file);
    int16_t r = WriteFile(filename, (uint8_t *)out.ptr, (uint32_t)out.len);
    PROF_END(write_file, "highlights-write");

#ifdef SWOS_SAVE_FORMAT_JSON_PROFILE
    SWOS_DBG("[json-prof-highlights-write] file_size: %u bytes\n",
            (unsigned)out.len);
#endif
    swos_io_free(out.ptr);
    if (r != 0) {
        ShowFsErrorByCode((uint16_t)r);
        return 1;
    }

    PROF_END(total, "highlights-write");

#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
    verify_roundtrip_region("highlights",
                            _hilFileBuffer, data_size,
                            verify_reload_highlights, filename, NULL);
#endif

    return 0;
}

int16_t SaveJson_LoadHighlights(uint8_t *filename, const uint8_t *src_unused,
                                uint32_t src_size_unused)
{
    (void)src_unused; (void)src_size_unused;

    PROF_START(total);

    PROF_START(read_file);
    uint8_t *src = NULL;
    uint32_t src_size = 0;
    if (read_save_file_heap(filename, &src, &src_size) != 0)
        return 1;
    PROF_END(read_file, "highlights-load");

#ifdef SWOS_SAVE_FORMAT_JSON_PROFILE
    SWOS_DBG("[json-prof-highlights-load] file_size: %u bytes\n",
            (unsigned)src_size);
#endif

    PROF_START(validate);
    long version = 0;
    if (mjson_get_long((const char *)src, (int)src_size,
                         "$.schema_version", &version) <= 0 ||
        (int)version != SAVE_JSON_SCHEMA_VERSION) {
        swos_io_free(src); return 1;
    }
    char kind[16] = {0};
    if (mjson_get_string((const char *)src, (int)src_size,
                         "$.file_kind", kind, sizeof(kind)) <= 0 ||
        strcmp(kind, KIND_HIGHLIGHTS) != 0) {
        swos_io_free(src); return 1;
    }
    PROF_END(validate, "highlights-load");

    string_pool_reset();
    long num = 0.0;

    HilFileHeader *hdr = (HilFileHeader *)_hilFileBuffer;

    PROF_START(header);
    if (mjson_get_long((const char *)src, (int)src_size,
                         "$.signature", &num) > 0)
        BE_WRITE_U32((uint8_t *)&hdr->signature, (uint32_t)num);
    if (mjson_get_long((const char *)src, (int)src_size,
                         "$.max_size", &num) > 0)
        BE_WRITE_U32((uint8_t *)&hdr->max_size,  (uint32_t)num);

    /* The rate the match was played at, so playback matches it (see the writer).
     * Absent in files written before this field existed -- those are 50 Hz Amiga
     * recordings, which is file_io.c's default, so leave it alone then. */
#ifdef SWOS_SDL_BUILD
    {
        /* SDL only: _replayPlaybackFps lives in dos_replay_load.c, which the m68k
         * build filters out (SDL_ONLY_SRCS). The Amiga plays at 50 Hz regardless,
         * so there is nothing for it to pace to. */
        extern int _replayPlaybackFps;
        long fps = 0;
        if (mjson_get_long((const char *)src, (int)src_size,
                             "$.replay_fps", &fps) > 0 && fps >= 1 && fps <= 240)
            _replayPlaybackFps = (int)fps;
    }
#endif

    load_team_game((const char *)src, (int)src_size,
                   "$.top_team",    (uint8_t *)&hdr->top_team);
    load_team_game((const char *)src, (int)src_size,
                   "$.bottom_team", (uint8_t *)&hdr->bottom_team);
    PROF_END(header, "highlights-load");

    memset(hdr->game_name,  0, sizeof(hdr->game_name));
    memset(hdr->game_round, 0, sizeof(hdr->game_round));
    char tmp[100];
    if (mjson_get_string((const char *)src, (int)src_size,
                         "$.game_name", tmp, sizeof(tmp)) > 0) {
        size_t n = strlen(tmp);
        if (n > sizeof(hdr->game_name)) n = sizeof(hdr->game_name);
        memcpy(hdr->game_name, tmp, n);
    }
    if (mjson_get_string((const char *)src, (int)src_size,
                         "$.game_round", tmp, sizeof(tmp)) > 0) {
        size_t n = strlen(tmp);
        if (n > sizeof(hdr->game_round)) n = sizeof(hdr->game_round);
        memcpy(hdr->game_round, tmp, n);
    }

    /* The 5 u16 header fields are written host-native here. The
     * legacy load path keeps bytes BE and LoadHilFile applies a
     * single BE16_TO_HOST swap on success (moved out of
     * highlights_menu.c so both paths converge on host-native
     * fields ready for the game to read). */
    uint16_t num_goals = 0;
    if (mjson_get_long((const char *)src, (int)src_size,
                         "$.num_goals", &num) > 0) num_goals = (uint16_t)num;
    hdr->num_goals = num_goals;
    _hilNumGoals    = num_goals;

#define HDR_U16(key, field) do { \
        if (mjson_get_long((const char *)src, (int)src_size, \
                             "$." key, &num) > 0) \
            hdr->field = (uint16_t)num; \
    } while (0)
    HDR_U16("team_1_goals",    team_1_goals);
    HDR_U16("team_2_goals",    team_2_goals);
    HDR_U16("pitch_number",    pitch_number);
    HDR_U16("max_substitutes", max_substitutes);
#undef HDR_U16

    /* Zero scene area then decode each scene. Slice the scenes
     * array once so per-scene mjson_find lookups scan a single
     * scene's slice instead of the whole file each iteration. */
    memset(_hilFileBuffer + HIL_HEADER_SIZE, 0,
           (size_t)num_goals * HIL_SCENE_SIZE);

    PROF_START(scenes);
    const char *scenes_arr = NULL;
    int scenes_arr_len = 0;
    if (mjson_find((const char *)src, (int)src_size, "$.scenes",
                   &scenes_arr, &scenes_arr_len) == MJSON_TOK_ARRAY) {
        /* Walk scenes via mjson_next so per-scene access is O(1)
         * in array position rather than O(i²·scene_size). */
        int pos = 0, koff, klen, voff, vlen, vt;
        uint16_t i = 0;
        while (i < num_goals &&
               (pos = mjson_next(scenes_arr, scenes_arr_len, pos,
                                 &koff, &klen, &voff, &vlen, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) { i++; continue; }
            load_scene_slice(scenes_arr + voff, vlen,
                             _hilFileBuffer + HIL_HEADER_SIZE
                                           + (uint32_t)i * HIL_SCENE_SIZE);
            i++;
        }
    }
    PROF_END(scenes, "highlights-load");

    swos_io_free(src);

    PROF_END(total, "highlights-load");
    return 0;
}
/* ---------- .TAC (tactics) ----------
 *
 * A single 370-byte TeamTactics struct: 9-byte name + 10 ×
 * PlayerPositions (each 5 × 7 bytes of packed x/y nibbles) + 10-byte
 * pairing table + 1-byte ballOutOfPlayTactics field.
 */

#define SAVE_TAC_DATA_SIZE 370u

static int16_t write_tactics_buf(uint8_t *filename, const uint8_t *src)
{
    JsonOutBuf out = { NULL, 0, 0, 0 };

    mjson_printf(json_out_emit, &out,
        "{\n  %Q:%d,\n  %Q:%Q,\n  ",
        "schema_version", SAVE_JSON_SCHEMA_VERSION,
        "file_kind",      KIND_TACTICS);

    /* name (9 bytes, NUL-padded) */
    {
        size_t n = 0;
        while (n < 9 && src[offsetof(TeamTactics, name) + n] != '\0') n++;
        mjson_printf(json_out_emit, &out, "%Q:%.*Q",
                     "name", (int)n,
                     (const char *)(src + offsetof(TeamTactics, name)));
    }

    /* 10 × 35-byte PlayerPositions arrays. Flat byte arrays for
     * compactness — interpretation is packed x/y nibbles per ball
     * zone, which the editor / match sim know how to unpack. */
    mjson_printf(json_out_emit, &out, ",\n  %Q:[", "player_positions");
    for (int i = 0; i < 10; i++) {
        if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
        const uint8_t *p = src + offsetof(TeamTactics, positions)
                              + (size_t)i * sizeof(PlayerPositions);
        mjson_printf(json_out_emit, &out, "%s", "[");
        for (int j = 0; j < (int)sizeof(PlayerPositions); j++) {
            if (j > 0) mjson_printf(json_out_emit, &out, "%s", ",");
            mjson_printf(json_out_emit, &out, "%u", (unsigned)p[j]);
        }
        mjson_printf(json_out_emit, &out, "%s", "]");
    }
    mjson_printf(json_out_emit, &out, "%s", "]");

    /* pairIds (10 bytes) + ball_out_of_play_tactics (1 byte). */
    mjson_printf(json_out_emit, &out, ",\n  %Q:[", "pair_ids");
    for (int i = 0; i < 10; i++) {
        if (i > 0) mjson_printf(json_out_emit, &out, "%s", ",");
        mjson_printf(json_out_emit, &out, "%u",
                     (unsigned)src[offsetof(TeamTactics, pairIds) + i]);
    }
    mjson_printf(json_out_emit, &out, "%s", "],\n  ");
    mjson_printf(json_out_emit, &out, "%Q:%u\n}\n",
                 "ball_out_of_play_tactics",
                 (unsigned)src[offsetof(TeamTactics, ballOutOfPlayTactics)]);

    if (out.oom || !out.ptr) {
        if (out.ptr) swos_io_free(out.ptr);
        return 1;
    }
    int16_t r = WriteFile(filename, (uint8_t *)out.ptr, (uint32_t)out.len);
    swos_io_free(out.ptr);
    if (r != 0) {
        ShowFsErrorByCode((uint16_t)r);
        return 1;
    }

#ifdef SWOS_SAVE_FORMAT_JSON_VERIFY
    /* TeamTactics is a caller-provided buffer (not a global), so we
     * snapshot the 370 bytes locally and round-trip through the
     * load path with a scratch destination. */
    {
        uint8_t scratch[SAVE_TAC_DATA_SIZE];
        memset(scratch, 0, sizeof(scratch));
        if (SaveJson_LoadTactics(filename, scratch, sizeof(scratch)) == 0) {
            size_t mismatches = 0, first = (size_t)-1;
            for (uint32_t i = 0; i < SAVE_TAC_DATA_SIZE; i++) {
                if (scratch[i] != src[i]) {
                    if (mismatches < 20) {
                        if (first == (size_t)-1) first = i;
                        SWOS_DBG(
                                "[json-verify-tactics] mismatch at +%u: loaded=0x%02X saved=0x%02X\n",
                                (unsigned)i, scratch[i], src[i]);
                    }
                    mismatches++;
                }
            }
            SWOS_DBG(
                    "[json-verify-tactics] %zu mismatches across %u bytes (first at +%zu)\n",
                    mismatches, (unsigned)SAVE_TAC_DATA_SIZE, first);
        }
    }
#endif

    return 0;
}

static int16_t load_tactics_buf(const char *src, int src_size, uint8_t *dst)
{
    long version = 0;
    if (mjson_get_long(src, src_size, "$.schema_version", &version) <= 0 ||
        (int)version != SAVE_JSON_SCHEMA_VERSION)
        return 1;

    char kind[16] = {0};
    if (mjson_get_string(src, src_size, "$.file_kind", kind, sizeof(kind)) <= 0 ||
        strcmp(kind, KIND_TACTICS) != 0)
        return 1;

    memset(dst, 0, SAVE_TAC_DATA_SIZE);

    /* name (9 bytes max) */
    char tmpname[16] = {0};
    if (mjson_get_string(src, src_size, "$.name", tmpname, sizeof(tmpname)) > 0) {
        size_t n = strlen(tmpname);
        if (n > 8) n = 8;
        memcpy(dst + offsetof(TeamTactics, name), tmpname, n);
    }

    /* 10 × 35-byte position arrays */
    for (int i = 0; i < 10; i++) {
        char path[64];
        swos_io_snprintf(path, sizeof(path), "$.player_positions[%d]", i);
        load_bytes_at(src, src_size, path,
                      dst, offsetof(TeamTactics, positions)
                            + (size_t)i * sizeof(PlayerPositions),
                      sizeof(PlayerPositions));
    }

    load_bytes_at(src, src_size, "$.pair_ids",
                  dst, offsetof(TeamTactics, pairIds), 10);

    long num = 0.0;
    if (mjson_get_long(src, src_size, "$.ball_out_of_play_tactics", &num) > 0)
        dst[offsetof(TeamTactics, ballOutOfPlayTactics)] = (uint8_t)(int)num;

    return 0;
}

int16_t SaveJson_WriteTactics(uint8_t *filename, const uint8_t *src, uint32_t src_size)
{
    if (src_size < SAVE_TAC_DATA_SIZE) return 1;
    return write_tactics_buf(filename, src);
}

int16_t SaveJson_LoadTactics(uint8_t *filename, uint8_t *dst, uint32_t dst_capacity)
{
    if (dst_capacity < SAVE_TAC_DATA_SIZE) return 1;

    uint8_t *src = NULL;
    uint32_t src_size = 0;
    if (read_save_file_heap(filename, &src, &src_size) != 0)
        return 1;

    int16_t r = load_tactics_buf((const char *)src, (int)src_size, dst);
    swos_io_free(src);
    return r;
}
