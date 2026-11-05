/* ======================================================================
 * translations.c — UI string localization.
 *
 * Loads data_json/translations/<lang>.json — a { "strings": { symbol: text } }
 * map — into a sorted table, and resolves a symbol name to its translated text
 * via tr() (bsearch). This is the string analogue of the menu/competition JSON:
 * the data files store the symbol NAME (e.g. "aDivision1"), and the language
 * file holds the text, so changing en.json (or shipping de.json/fr.json)
 * re-skins every name without touching the data or the code.
 *
 * Portable C (mjson + malloc + stdio) — back-port-clean; only the call sites are
 * SDL-gated for now. A missing language file is non-fatal: tr() returns NULL and
 * every caller falls back to the compiled string symbol.
 * ====================================================================== */

#include "translations.h"
#include "swos_io.h"   /* swos_data_path — rom:/ on the Switch */
#include "string_symbols.h"

#ifdef SWOS_SDL_BUILD   /* Runtime JSON translation is host-only: mjson + stdio +
                           malloc. The m68k Amiga toolchain has none of those, and
                           the Amiga plan is to bake JSON->C at build time, so the
                           #else stubs at the bottom make tr() behave as "no
                           translation file loaded" (compiled English strings)
                           until that bake step exists. */
#include "mjson.h"
#include "mods.h"       /* the enabled mods' strings, merged over the game's */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *sym; const char *text; } TrEntry;

static TrEntry *s_tr      = NULL;   /* sorted by sym (name → text) */
static int      s_trCount = 0;

/* Address → translated text: the compiled string symbol's &address mapped to its
 * localized text, so the renderer can localize a string it only holds as a
 * POINTER (e.g. menu string_table option values). Built by joining the
 * stringSymbols registry (name → &addr) with the loaded name → text table. */
typedef struct { void *addr; const char *text; } TrPtrEntry;
static TrPtrEntry *s_trp      = NULL;   /* sorted by addr */
static int         s_trpCount = 0;

static int tr_cmp(const void *a, const void *b)
{
    return strcmp(((const TrEntry *)a)->sym, ((const TrEntry *)b)->sym);
}
static int trp_cmp(const void *a, const void *b)
{
    void *x = ((const TrPtrEntry *)a)->addr, *y = ((const TrPtrEntry *)b)->addr;
    return (x > y) - (x < y);
}

static int tr_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Copy an mjson string token to a fresh NUL-terminated string, stripping the
 * surrounding quotes AND decoding the JSON escapes the exporter emits (\\ \" \/
 * \b \f \n \r \t and \uXXXX). SWOS uses a single-byte charset, so \uXXXX is
 * emitted as one byte (code & 0xFF): this losslessly round-trips the control /
 * high bytes a few compiled strings carry — a leading 0x01/0xFF render marker,
 * the 0x9C pound (£) glyph, the offer-letter leading 0x00 — so the pool string
 * is byte-identical to the compiled symbol. A raw copy (the old behaviour) would
 * instead render the literal escape text, e.g. "SOUTH AMERICA". The decode
 * is never longer than the source, so len+1 is a safe bound. */
static char *tr_dup(const char *tok, int len)
{
    if (len >= 2 && tok[0] == '"' && tok[len - 1] == '"') { tok++; len -= 2; }
    if (len < 0) len = 0;
    char *s = (char *)malloc((size_t)len + 1);
    if (!s) return NULL;
    int o = 0;
    for (int i = 0; i < len; i++) {
        char c = tok[i];
        if (c == '\\' && i + 1 < len) {
            char n = tok[++i];
            switch (n) {
            case 'n': s[o++] = '\n'; break;
            case 't': s[o++] = '\t'; break;
            case 'r': s[o++] = '\r'; break;
            case 'b': s[o++] = '\b'; break;
            case 'f': s[o++] = '\f'; break;
            case 'u': {
                int h0, h1, h2, h3;
                if (i + 4 < len &&
                    (h0 = tr_hex(tok[i + 1])) >= 0 && (h1 = tr_hex(tok[i + 2])) >= 0 &&
                    (h2 = tr_hex(tok[i + 3])) >= 0 && (h3 = tr_hex(tok[i + 4])) >= 0) {
                    s[o++] = (char)((((h0 << 12) | (h1 << 8) | (h2 << 4) | h3)) & 0xFF);
                    i += 4;
                } else {
                    s[o++] = n;   /* malformed — emit literally */
                }
                break;
            }
            default: s[o++] = n; break;   /* \\ \" \/ and anything else */
            }
        } else {
            unsigned char uc = (unsigned char)c;
            if (uc >= 0xC0) {
                /* Raw UTF-8 multi-byte (a hand-edited fr.json/de.json with real
                 * accented characters, not \uXXXX escapes): decode to the code
                 * point and keep its low byte — Latin-1, which IS the game's
                 * single-byte charset (É U+00C9 -> 0xC9, matching the font /
                 * conversionTable). Code points above 0xFF that DO have a font
                 * glyph are mapped to a spare byte (French Œ U+0152 -> 0x8C,
                 * Ÿ U+0178 -> 0x9E, euro € U+20AC -> 0x9D; see swos_font.py
                 * CP_OVERRIDE); any other >0xFF point falls back to (cp & 0xFF). */
                int extra = uc >= 0xF0 ? 3 : uc >= 0xE0 ? 2 : 1;
                int cp = uc & (0x3F >> extra), ok = 1;
                for (int k = 0; k < extra; k++) {
                    unsigned char cc = (i + 1 < len) ? (unsigned char)tok[i + 1] : 0;
                    if ((cc & 0xC0) != 0x80) { ok = 0; break; }   /* not a continuation byte */
                    cp = (cp << 6) | (cc & 0x3F);
                    i++;
                }
                int b = cp == 0x152 ? 0x8C : cp == 0x178 ? 0x9E
                        : cp == 0x20AC ? 0x9D : (cp & 0xFF);
                s[o++] = ok ? (char)b : (char)uc;
            } else {
                s[o++] = c;   /* ASCII (and stray bytes) pass through */
            }
        }
    }
    s[o] = '\0';
    return s;
}

/* Look up a string symbol's compiled &address by name (startup-only, linear). */
static void *sym_addr(const char *name)
{
    for (int i = 0; i < _stringSymbolsCount; i++)
        if (strcmp(_stringSymbols[i].name, name) == 0)
            return _stringSymbols[i].addr;
    return NULL;
}

/* Public: resolve a string-symbol NAME to the ADDRESS of the compiled string it
 * names. Backed by _stringSymbols (compiled data, always present — independent
 * of whether a translation file loaded), so it works even when tr() returns
 * NULL. Used as the tr()==NULL fallback for data-driven names. */
const void *Translations_SymbolAddr(const char *name)
{
    return name ? sym_addr(name) : NULL;
}

/* A mod's translations (include/mods.h): data_json/translations/<lang>.json in its folder, the
 * same shape as the game's, holding only the strings it adds or changes. Merged over the game's
 * -- each key it has replaces or joins the table -- so two mods can each add their own. */
static void merge_mod_strings(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n > 0 ? (char *)malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return; }
    fclose(f);
    const char *strs; int sl, o = 0, ko, kl, vo, vl, vt, added = 0, changed = 0;
    if (mjson_find(buf, (int)n, "$.strings", &strs, &sl) == MJSON_TOK_OBJECT) {
        while ((o = mjson_next(strs, sl, o, &ko, &kl, &vo, &vl, &vt)) != 0) {
            if (vt != MJSON_TOK_STRING) continue;
            char *sym = tr_dup(strs + ko, kl), *text = tr_dup(strs + vo, vl);
            if (!sym || !text) { free(sym); free(text); continue; }
            TrEntry key = { sym, NULL };
            TrEntry *hit = s_trCount ? (TrEntry *)bsearch(&key, s_tr, (size_t)s_trCount, sizeof *s_tr, tr_cmp) : NULL;
            if (hit) {                                  /* (the old text stays: tr_ptr may hold it) */
                hit->text = text;
                free(sym);
                changed++;
            } else {
                TrEntry *grown = (TrEntry *)realloc(s_tr, (size_t)(s_trCount + 1) * sizeof *s_tr);
                if (!grown) { free(sym); free(text); continue; }
                s_tr = grown;
                s_tr[s_trCount].sym = sym;
                s_tr[s_trCount].text = text;
                s_trCount++;
                qsort(s_tr, (size_t)s_trCount, sizeof *s_tr, tr_cmp);
                added++;
            }
        }
    }
    free(buf);
    fprintf(stderr, "[i18n] %s: %d strings changed, %d added\n", path, changed, added);
}

void Translations_Load(const char *lang)
{
    s_tr = NULL; s_trCount = 0;
    if (!lang || !lang[0]) lang = "en";

    char path[160];
    snprintf(path, sizeof path, "data_json/translations/%s.json", lang);
    char anchored[224];
    /* The game's own file (not a mod's: theirs are merged over it below). */
    FILE *f = fopen(swos_data_path_base(path, anchored, (uint32_t)sizeof anchored), "rb");
    if (!f) {
        fprintf(stderr, "[i18n] %s not found — using compiled strings\n", path);
        fflush(stderr);
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return; }
    char *buf = (char *)malloc((size_t)n);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return; }

    const char *strs; int sl;
    if (mjson_find(buf, (int)n, "$.strings", &strs, &sl) != MJSON_TOK_OBJECT) {
        free(buf);
        return;
    }
    /* count members, then fill */
    int cap = 0, o = 0, ko, kl, vo, vl, vt;
    while ((o = mjson_next(strs, sl, o, &ko, &kl, &vo, &vl, &vt)) != 0) cap++;
    if (cap <= 0) { free(buf); return; }
    s_tr = (TrEntry *)malloc((size_t)cap * sizeof *s_tr);
    if (!s_tr) { free(buf); return; }

    o = 0;
    int k = 0;
    while ((o = mjson_next(strs, sl, o, &ko, &kl, &vo, &vl, &vt)) != 0 && k < cap) {
        if (vt != MJSON_TOK_STRING) continue;
        char *sym  = tr_dup(strs + ko, kl);
        char *text = tr_dup(strs + vo, vl);
        if (sym && text) { s_tr[k].sym = sym; s_tr[k].text = text; k++; }
        else { free(sym); free(text); }
    }
    s_trCount = k;
    qsort(s_tr, (size_t)s_trCount, sizeof *s_tr, tr_cmp);

    /* The enabled mods' strings, the last mod first so the first one's win (as its files do). */
    for (int32_t m = Mods_Count() - 1; m >= 0; m--) {
        char mp[1300];
        snprintf(mp, sizeof mp, "%s/%s", Mods_Dir(m), path);
        merge_mod_strings(mp);
    }

    /* Build the address → text map: each compiled string symbol's &address →
     * its localized text. Lets the renderer localize by pointer (tr_ptr). */
    int nCountry = 0;
    if (_stringSymbolsCount > 0) {
        s_trp = (TrPtrEntry *)malloc((size_t)_stringSymbolsCount * sizeof *s_trp);
        if (s_trp) {
            int m = 0;
            for (int si = 0; si < _stringSymbolsCount; si++) {
                const char *txt = tr(_stringSymbols[si].name);
                if (txt) { s_trp[m].addr = _stringSymbols[si].addr; s_trp[m].text = txt; m++; }
            }
            /* Country names: rebuild the packed "<flag>NAME\0NAT\0" string from the
             * clean [name, nationality] pair (preserving the compiled flag byte),
             * and address-map the symbol to it so tr_ptr / Countries_Localize use
             * it. Stored packed because the consumers read `str + 1` (skip flag)
             * up to the NUL, then the nationality after it. */
            const char *carr; int cal;
            if (m < _stringSymbolsCount &&
                mjson_find(buf, (int)n, "$.countries", &carr, &cal) == MJSON_TOK_OBJECT) {
                int co = 0, cko, ckl, cvo, cvl, cvt;
                while (m < _stringSymbolsCount &&
                       (co = mjson_next(carr, cal, co, &cko, &ckl, &cvo, &cvl, &cvt)) != 0) {
                    if (cvt != MJSON_TOK_ARRAY) continue;
                    const char *st = carr + cko; int sn = ckl;
                    if (sn >= 2 && st[0] == '"' && st[sn - 1] == '"') { st++; sn -= 2; }
                    if (sn <= 0 || sn >= 64) continue;
                    char sym[64]; memcpy(sym, st, (size_t)sn); sym[sn] = 0;
                    void *addr = sym_addr(sym);
                    if (!addr) continue;
                    char name[48], nat[48];
                    if (mjson_get_string(carr + cvo, cvl, "$[0]", name, (int)sizeof name) < 0) name[0] = 0;
                    if (mjson_get_string(carr + cvo, cvl, "$[1]", nat,  (int)sizeof nat ) < 0) nat[0]  = 0;
                    size_t ln = strlen(name), lt = strlen(nat);
                    char *packed = (char *)malloc(1 + ln + 1 + lt + 1);
                    if (!packed) continue;
                    packed[0] = *(const char *)addr;                 /* compiled flag byte */
                    memcpy(packed + 1, name, ln); packed[1 + ln] = 0;
                    memcpy(packed + 2 + ln, nat, lt); packed[2 + ln + lt] = 0;
                    s_trp[m].addr = addr; s_trp[m].text = packed; m++; nCountry++;
                }
            }
            s_trpCount = m;
            qsort(s_trp, (size_t)s_trpCount, sizeof *s_trp, trp_cmp);
        }
    }
    free(buf);   /* tr_dup / packed strings copied everything we keep */

    fprintf(stderr, "[i18n] loaded %d strings + %d countries from %s (%d address-mapped)\n",
            s_trCount, nCountry, path, s_trpCount);
    fflush(stderr);
}

const char *tr(const char *symbol)
{
    if (!s_tr || !symbol) return NULL;
    TrEntry key;
    key.sym = symbol;
    key.text = NULL;
    TrEntry *e = (TrEntry *)bsearch(&key, s_tr, (size_t)s_trCount, sizeof *s_tr, tr_cmp);
    return e ? e->text : NULL;
}

const char *tr_ptr(const void *strAddr)
{
    if (!s_trp || !strAddr) return NULL;
    TrPtrEntry key;
    key.addr = (void *)strAddr;
    key.text = NULL;
    TrPtrEntry *e = (TrPtrEntry *)bsearch(&key, s_trp, (size_t)s_trpCount, sizeof *s_trp, trp_cmp);
    return e ? e->text : NULL;
}

void Countries_Localize(void)
{
    /* countriesTable[262]: country-code → packed "<flag>NAME\0NAT" string, read
     * by ~8 display sites (team select, results, stats, …). Repoint each entry
     * to its localized packed string so every site shows the active language.
     * countryNames[64] is zero-init and filled at runtime from this table, so it
     * inherits the translation. */
    extern void *_countriesTable[];
    int n = 0;
    for (int i = 0; i < 262; i++) {
        if (!_countriesTable[i]) continue;
        const char *t = tr_ptr(_countriesTable[i]);
        if (t) { _countriesTable[i] = (void *)t; n++; }
    }
    fprintf(stderr, "[i18n] localized %d country names\n", n);
    fflush(stderr);
}

#else  /* Amiga: no runtime JSON loader (see the #ifdef SWOS_SDL_BUILD above). Every
          entry point behaves as "nothing loaded" — tr/tr_ptr return NULL so each
          caller falls back to its compiled English string, and load/localize are
          no-ops. Matches the documented missing-file behaviour in translations.h. */
void Translations_Load(const char *lang) { (void)lang; }
const char *tr(const char *symbol) { (void)symbol; return 0; }
const char *tr_ptr(const void *strAddr) { (void)strAddr; return 0; }
void Countries_Localize(void) { }

#endif /* SWOS_SDL_BUILD */
