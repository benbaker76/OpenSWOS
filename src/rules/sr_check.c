/*
 * sr_check.c -- untrusted input: a pack's knockouts (compiled spec bytes) and the state a game
 * saved, checked once, before any rule reads them.
 *
 * The rules (sr_gplay.c, sr_dplay.c, ...) stay the Mod's code line for line, with its fixed
 * buffers: a play-off's entrants in 64 slots, a round's members in 128, a group's rows in 32, a
 * stage table's in 32. Bytes from the Mod's own tools never exceed them; a spec written by hand, or
 * a damaged save, can. These walks reject such input up front -- every count over its buffer, every
 * index past what exists, every byte with no meaning -- with an error code, the play-off, the byte
 * and the field's name, so the play code needs no check of its own.
 *
 * Copyright (c) 2026 Davide Lorigliola, Ben Baker. MIT licence (LICENSE).
 */
#include "swosrules.h"

typedef uint8_t u8;

enum { GN = 64, GT = SR_CLUBS, BEST_LOSER = 0x3F };

static int32_t fail(sr_check_error *err, int32_t code, int32_t pidx, uint32_t offset, const char *field)
{
    if (err) {
        err->code = code;
        err->pidx = pidx;
        err->offset = offset;
        err->field = field;
    }
    return code;
}

int32_t sr_spec_check(const uint8_t *spec, uint32_t len, int32_t divisions, sr_check_error *err)
{
    uint32_t at = 0;
    const int32_t nd = divisions > 0 && divisions < SR_DIVS ? divisions : SR_DIVS;
    if (err) { err->code = SR_CHECK_OK; err->pidx = -1; err->offset = 0; err->field = 0; }
    if (!spec)
        return fail(err, SR_CHECK_TRUNCATED, -1, 0, "spec");
    for (int32_t pidx = 0;; pidx++) {
        if (at >= len)
            return fail(err, SR_CHECK_TRUNCATED, pidx, at, "end of the play-offs (a 0)");
        if (spec[at] == 0)
            return SR_CHECK_OK;
        if (len - at < 6)
            return fail(err, SR_CHECK_TRUNCATED, pidx, at, "play-off header");
        const u8 *s = spec + at;
        const int32_t kind = s[0], n = s[1], nr = s[2];
        if (kind < 1 || kind > 8)
            return fail(err, SR_CHECK_VALUE, pidx, at, "kind");
        if (kind == 7) {                        /* the promedio: matches, loser_div; no entrants, no rounds */
            if (n || nr || s[3] == 0 || s[4] == 0 || s[4] >= nd)
                return fail(err, SR_CHECK_VALUE, pidx, at, "promedio (matches, loser_div)");
            at += 6;
            continue;
        }
        if (n > GN)
            return fail(err, SR_CHECK_COUNT, pidx, at + 1, "entrants (at most 64)");
        if (nr < 1 || nr > 16)
            return fail(err, SR_CHECK_COUNT, pidx, at + 2, "rounds (1..16)");
        at += 6;
        if (len - at < 4u * (uint32_t)n)
            return fail(err, SR_CHECK_TRUNCATED, pidx, at, "entrants");
        for (int32_t i = 0; i < n; i++, at += 4) {
            const u8 *e = spec + at;
            const int32_t ty = e[0];
            if (ty > 8)
                return fail(err, SR_CHECK_VALUE, pidx, at, "entrant type");
            if ((ty == 0 || ty == 3 || ty == 5 || ty == 6 || ty == 7) && e[1] >= nd)
                return fail(err, SR_CHECK_INDEX, pidx, at + 1, "entrant division");
            if (ty == 0 && e[2] < 1)
                return fail(err, SR_CHECK_INDEX, pidx, at + 2, "entrant place (from 1)");
            if (ty == 3 && !(e[3] < 2 || (e[3] >= 4 && e[3] < 4 + SR_STAGE_TABLES)))
                return fail(err, SR_CHECK_INDEX, pidx, at + 3, "entrant stage table");
        }
        int32_t np = 0, nt = 0;                 /* placings of the group rounds so far, ties so far */
        for (int32_t k = 0; k < nr; k++) {
            if (len - at < 6)
                return fail(err, SR_CHECK_TRUNCATED, pidx, at, "round header");
            const u8 *r = spec + at;
            const int32_t ties = r[0], legs = r[1], host = r[2] & 0x7F, level = r[3];
            const int32_t group = legs & 0x80, members = group ? ties : 2 * ties;
            if (group ? ties > GT : ties > GN)
                return fail(err, SR_CHECK_COUNT, pidx, at, group ? "group members (at most 32)" : "ties (at most 64)");
            if (group ? (legs & 0x7F) > 8 : (legs < 1 || legs > 3))
                return fail(err, SR_CHECK_VALUE, pidx, at + 1, group ? "meetings (0..8)" : "legs (1..3)");
            if (host > 3)
                return fail(err, SR_CHECK_VALUE, pidx, at + 2, "host");
            if (level > 5)
                return fail(err, SR_CHECK_VALUE, pidx, at + 3, "level");
            if (!group && (r[4] & 0x7F) > 2 * ties)
                return fail(err, SR_CHECK_COUNT, pidx, at + 4, "pairing (at most the round's clubs)");
            at += 6;
            if (len - at < 2u * (uint32_t)ties)
                return fail(err, SR_CHECK_TRUNCATED, pidx, at, "members");
            for (int32_t i = 0; i < members; i++) {
                const int32_t c = spec[at + i];
                const int32_t ok = c == BEST_LOSER ? k > 0 && nt > 0
                                 : c < 0x40 ? c < n
                                 : c < 0x80 ? (c & 0x3F) < np
                                 : (c & 0x3F) < nt;
                if (!ok)
                    return fail(err, SR_CHECK_INDEX, pidx, at + (uint32_t)i, "member");
            }
            if (group) {
                if (np + ties > GN)
                    return fail(err, SR_CHECK_COUNT, pidx, at, "placings (at most 64)");
                np += ties;
            } else {
                if (nt + ties > GN)
                    return fail(err, SR_CHECK_COUNT, pidx, at, "ties of the play-off (at most 64)");
                nt += ties;
            }
            at += 2u * (uint32_t)ties;
        }
    }
}

int32_t sr_season_check(const sr_season *se, sr_check_error *err)
{
    if (err) { err->code = SR_CHECK_OK; err->pidx = -1; err->offset = 0; err->field = 0; }
    if (!se)
        return SR_CHECK_OK;
    if (se->bank) {
        for (int32_t k = 0; k < SR_STAGE_TABLES; k++)
            if (se->bank->n[k] > SR_CLUBS)
                return fail(err, SR_CHECK_COUNT, -1, (uint32_t)k, "stage table's clubs (at most 32)");
        if (se->bank->n_kept > SR_CLUBS)
            return fail(err, SR_CHECK_COUNT, -1, 0, "kept table's clubs (at most 32)");
        if (se->bank->n_kept && se->bank->kept_div >= SR_DIVS)
            return fail(err, SR_CHECK_INDEX, -1, 0, "kept table's division");
    }
    if (se->promedio && se->promedio->n > SR_PROMEDIO_CLUBS)
        return fail(err, SR_CHECK_COUNT, -1, 0, "promedio's clubs (at most 48)");
    if (se->winners && se->winners->n > SR_WINNERS_MAX)
        return fail(err, SR_CHECK_COUNT, -1, 0, "SEASON WINNERS entries (at most 40)");
    if (se->levels && (se->levels->levels > SR_DIVS || (se->levels->levels && !se->levels->first)))
        return fail(err, SR_CHECK_COUNT, -1, 0, "levels");
    return SR_CHECK_OK;
}

int32_t sr_league_check(const sr_league *lg, sr_check_error *err)
{
    if (err) { err->code = SR_CHECK_OK; err->pidx = -1; err->offset = 0; err->field = 0; }
    if (!lg)
        return SR_CHECK_OK;
    if (lg->divisions > SR_DIVS)
        return fail(err, SR_CHECK_COUNT, -1, 0, "divisions (at most 16)");
    if (lg->italy && (lg->groups < 2 || lg->groups > 4 || lg->group_first + lg->groups > lg->divisions ||
                      lg->serie_b >= lg->divisions))
        return fail(err, SR_CHECK_INDEX, -1, 0, "the Italian rules' divisions (Serie B, Serie C's groups)");
    if (lg->seconds && !lg->second)
        return fail(err, SR_CHECK_COUNT, -1, 0, "B teams");
    if (lg->spec && !lg->spec_len)
        return fail(err, SR_CHECK_COUNT, -1, 0, "spec_len (a spec's bytes: required)");
    if (lg->spec)
        return sr_spec_check(lg->spec, lg->spec_len, lg->divisions, err);
    return SR_CHECK_OK;
}
