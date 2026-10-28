/* ======================================================================
 * menu_load_json.c — JSON menu loader (SDL build), docs/MENUS.md §2d
 *
 * Reads bin/data_json/menus/<name>.json and builds the packed "§0"
 * byte-stream that UnpackMenu (menu_system.c) walks — the SAME stream the
 * compiled static `Menu_Foo fooMenu = {…}` initializers lay down. Pointer
 * payloads (callbacks, strings, string-tables, sprite-copy structs, buffers)
 * are stored in the JSON as SYMBOL NAMES and re-resolved here through the
 * generated registry (menu_symbols.c / menu_resolve) — the menu analogue of
 * the save format's "no addresses on disk" rule.
 *
 * This increment is VERIFY-FIRST (the SWOS_SAVE_FORMAT_JSON_VERIFY discipline):
 * MenuJson_VerifyAll() builds each menu from JSON and memcmp's it against the
 * compiled static bytes. A zero-diff across all 47 menus is the green light to
 * retire the static menu_data.c initializers (MENUS.md §2d/§3b). Gated on env
 * SWOS_MENU_JSON; without it nothing runs and the static menus stay
 * authoritative, so the migration is incremental and reversible.
 *
 * Byte order is HOST-NATIVE here: the SDL build reads back its own freshly
 * built buffer, and UnpackMenu reads pointers via memcpy of sizeof(uintptr_t).
 * The Amiga (BE, 4-byte ptr) path is the Python emitter, docs/MENUS.md §3.
 * ====================================================================== */

#include "mjson.h"
#include "swos_io.h"   /* swos_data_path — rom:/ on the Switch */
#include "menu_symbols.h"
#include "menu_load_json.h"
#include "translations.h"
#include "menu_system.h"   /* MENU_FLAGS_MARKER; UnpackMenu for MenuJson_VerifyDynamic */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- packed-stream markers / opcode count (mirror UnpackMenu) ---- */
#define MK_END        0xFC19u   /* 64537 */
#define MK_SET_ORIGIN 0xFC1Au   /* 64538 */
#define MK_TEMPLATE   0xFC1Bu   /* 64539 — start template */
#define MK_RESET_TMPL 0xFC1Cu   /* 64540 — reset template */
#define MK_COORDS_BYTE 0xFFFFu  /* compact coords */
#define MK_COORDS_WIDE 0xFFFEu  /* word coords (default) */

/* ======================================================================
 * Byte writer — host-native little-endian on the SDL build.
 * ====================================================================== */
typedef struct { uint8_t *p; uint8_t *end; int overflow; } MWriter;

static void w8(MWriter *w, unsigned v)
{
    if (w->p + 1 > w->end) { w->overflow = 1; return; }
    *w->p++ = (uint8_t)v;
}
static void w16(MWriter *w, unsigned v)
{
    if (w->p + 2 > w->end) { w->overflow = 1; return; }
    w->p[0] = (uint8_t)(v & 0xFF);
    w->p[1] = (uint8_t)((v >> 8) & 0xFF);
    w->p += 2;
}
static void w32(MWriter *w, uint32_t v)
{
    if (w->p + 4 > w->end) { w->overflow = 1; return; }
    w->p[0] = (uint8_t)(v & 0xFF);
    w->p[1] = (uint8_t)((v >> 8) & 0xFF);
    w->p[2] = (uint8_t)((v >> 16) & 0xFF);
    w->p[3] = (uint8_t)((v >> 24) & 0xFF);
    w->p += 4;
}
/* Native pointer-width slot — UnpackMenu reads these via memcpy(sizeof(uintptr_t)). */
static void wptr(MWriter *w, void *ptr)
{
    if (w->p + sizeof(uintptr_t) > w->end) { w->overflow = 1; return; }
    memcpy(w->p, &ptr, sizeof(uintptr_t));
    w->p += sizeof(uintptr_t);
}

/* ---- packed-stream reader: walk a compiled static menu to its end marker to
 *      learn its byte length (the in-place install's overflow bound). Mirrors
 *      the parse in UnpackMenu / the build above, payload-width for payload-
 *      width, so it can never disagree about how far a menu extends. ---- */
static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static int op_payload_width(int op)
{
    switch (op) {
    case 1:  return 0;                              /* invisible */
    case 2:  return 4;                              /* nav: 4×i8 */
    case 4: case 5: case 6: case 9: return 2;       /* colors/sprite_bg/invalid/sprite_fg: u16 */
    case 12: return 4;                              /* number: u16+u16 */
    case 17: case 18: case 19: case 20: return 2;   /* skips: u8+u8 */
    case 22: return 2;                              /* text_no_ptr: u16 */
    case 3: case 7: case 13: case 15: case 16: case 21:
        return (int)sizeof(uintptr_t);              /* ptr-only opcodes */
    case 8: case 10: case 11: case 14:
        return 2 + (int)sizeof(uintptr_t);          /* u16 flags/mask + ptr */
    default: return 0;
    }
}

static int packed_source_len(const uint8_t *m)
{
    const uint8_t *p = m + 3 * sizeof(uintptr_t) + 4;   /* header: 3 ptr + u32 defaultEntry */
    if (rd16(p) == MENU_FLAGS_MARKER) p += 4;           /* optional flags word (§0a) */
    int byteMode = 0;
    for (;;) {
        unsigned w = rd16(p);
        if (w == MK_END)        { p += 2; break; }
        if (w == MK_SET_ORIGIN) { p += 6; continue; }
        if (w == MK_RESET_TMPL) { p += 2; continue; }
        if (w == MK_TEMPLATE)   { p += 2; }            /* then parse the template entry */
        unsigned w0 = rd16(p);
        if (w0 == MK_COORDS_BYTE) { byteMode = 1; p += 2; }
        else if (w0 == MK_COORDS_WIDE) { byteMode = 0; p += 2; }
        p += byteMode ? 4 : 8;                          /* coords */
        for (;;) {
            unsigned op = rd16(p); p += 2;
            if (op == 0) break;                         /* MENU_CMD_END */
            p += op_payload_width((int)op);
        }
    }
    return (int)(p - m);
}

/* ======================================================================
 * mjson field readers — integers may be JSON numbers OR hex strings ("0x10").
 * Uses mjson_get_long (integer-only; works on the FPU-less m68k back-port too).
 * ====================================================================== */
static int mj_long(const char *j, int jl, const char *path, long *out)
{
    const char *tp; int tl;
    int t = mjson_find(j, jl, path, &tp, &tl);
    if (t == MJSON_TOK_NUMBER)
        return mjson_get_long(j, jl, path, out);
    if (t == MJSON_TOK_STRING) {
        char tmp[24];
        if (mjson_get_string(j, jl, path, tmp, (int)sizeof tmp) <= 0) return 0;
        *out = strtol(tmp, NULL, 0);        /* base 0 → 0x.. and decimal */
        return 1;
    }
    return 0;
}
static long mj_long_def(const char *j, int jl, const char *path, long def)
{
    long v;
    return mj_long(j, jl, path, &v) ? v : def;
}
static int mj_bool_def(const char *j, int jl, const char *path, int def)
{
    int v;
    return mjson_get_bool(j, jl, path, &v) ? v : def;
}

/* Emit a pointer payload from a field that is a symbol NAME (resolved via the
 * registry), a literal INTEGER (a small deferred-string sentinel 1..255 that
 * UnpackMenu rewrites to 0xFFFFFFFF), or null/absent (a NULL pointer). */
static void wfield_ptr(MWriter *w, const char *j, int jl, const char *path,
                       const char *menuName, int translate, int isFunc);

/* When set (during MenuJson_VerifyAll), wfield_ptr resolves text symbols to their
 * COMPILED address (mj_sym) instead of the translated string, so the JSON-built menu
 * byte-matches the untranslated static menu_data.c. This proves 1:1 faithfulness: the
 * runtime tr() pointers are an i18n display detail, and the Amiga JSON->C bake emits the
 * symbol name (reproducing the original menu_data.c regardless of translation). */
static int s_menu_verify_raw = 0;

/* Resolve a name field ("$.func"/"$.str"/…) to its address; NULL if the field
 * is absent / JSON null / unresolved (a missing symbol is logged, not fatal). */
/* Symbol tables registered at run time by modules whose menus exist only as JSON
 * (MenuJson_RegisterSymbols), searched after the generated registry. */
#define MAX_EXTRA_SYMBOL_TABLES 8
static struct { const MenuSymbol *syms; int count; } s_extraSymbols[MAX_EXTRA_SYMBOL_TABLES];
static int s_numExtraSymbolTables;

void MenuJson_RegisterSymbols(const MenuSymbol *syms, int count)
{
    if (s_numExtraSymbolTables == MAX_EXTRA_SYMBOL_TABLES) {
        fprintf(stderr, "[menu-json] RegisterSymbols: all %d table slots used, %d symbols NOT registered\n",
                MAX_EXTRA_SYMBOL_TABLES, count);
        return;
    }
    s_extraSymbols[s_numExtraSymbolTables].syms = syms;
    s_extraSymbols[s_numExtraSymbolTables].count = count;
    s_numExtraSymbolTables++;
}

static void *resolve_symbol(const char *name)
{
    void *p = menu_resolve(name);
    for (int t = 0; !p && t < s_numExtraSymbolTables; t++) {
        for (int i = 0; i < s_extraSymbols[t].count; i++) {
            if (strcmp(s_extraSymbols[t].syms[i].name, name) == 0) {
                p = s_extraSymbols[t].syms[i].ptr;
                break;
            }
        }
    }
    return p;
}

/* Set while MenuJson_Load builds a menu that has no compiled counterpart. There an
 * unresolved HANDLER becomes a no-op instead of NULL: an imported SWOS 2020 menu
 * names handlers OpenSWOS has not ported yet, and a NULL custom-draw function
 * would be called. Compiled menus keep NULL, byte-identical to menu_data.c. */
static int s_dynamicBuild = 0;

static void unported_handler(void)
{
}

/* Returns 1 the first time a handler name is seen, so each unported handler is
 * logged once however many entries name it (a list row repeats one handler). */
static int first_unported_sighting(const char *name)
{
    static char seen[256][80];      /* mj_sym's name buffer is 80 */
    static int numSeen;
    for (int i = 0; i < numSeen; i++)
        if (strcmp(seen[i], name) == 0)
            return 0;
    if (numSeen < (int)(sizeof seen / sizeof seen[0]))
        snprintf(seen[numSeen++], sizeof seen[0], "%s", name);
    return 1;
}

/* Resolve a name field ("$.func"/"$.str"/…) to its address; NULL if the field
 * is absent / JSON null / unresolved (a missing symbol is logged, not fatal). */
static void *mj_sym(const char *j, int jl, const char *path, const char *menuName, int isFunc)
{
    const char *tp; int tl;
    if (mjson_find(j, jl, path, &tp, &tl) != MJSON_TOK_STRING)
        return NULL;
    char name[80];
    if (mjson_get_string(j, jl, path, name, (int)sizeof name) <= 0)
        return NULL;
    void *p = resolve_symbol(name);
    if (!p) {
        if (isFunc && s_dynamicBuild) {
            if (first_unported_sighting(name))
                fprintf(stderr, "[menu-json] %s: handler '%s' not ported yet (no-op)\n", menuName, name);
            return (void *)unported_handler;
        }
        fprintf(stderr, "[menu-json] %s: unresolved symbol '%s'\n", menuName, name);
    }
    return p;
}

static void wfield_ptr(MWriter *w, const char *j, int jl, const char *path,
                       const char *menuName, int translate, int isFunc)
{
    const char *tp; int tl;
    int t = mjson_find(j, jl, path, &tp, &tl);
    if (t == MJSON_TOK_NUMBER) {           /* literal deferred-string sentinel (e.g. 4) */
        long v = 0;
        mjson_get_long(j, jl, path, &v);
        if (w->p + sizeof(uintptr_t) > w->end) { w->overflow = 1; return; }
        uintptr_t pv = (uintptr_t)(intptr_t)v;
        memcpy(w->p, &pv, sizeof pv);
        w->p += sizeof pv;
        return;
    }
    if (t == MJSON_TOK_STRING) {           /* symbol name */
        if (translate && !s_menu_verify_raw) {
            /* Display text: resolve the symbol's text for the active language
             * from translations/<lang>.json; fall back to the compiled string. */
            char name[80];
            if (mjson_get_string(j, jl, path, name, (int)sizeof name) > 0) {
                const char *txt = tr(name);
                if (txt) { wptr(w, (void *)txt); return; }
            }
        }
        wptr(w, mj_sym(j, jl, path, menuName, isFunc));   /* → compiled string/symbol address */
    } else {
        wptr(w, NULL);                     /* null / absent → NULL pointer */
    }
}

/* SWOS 2020 menus carry their text inline -- `"literal": "EXIT"` -- where ours
 * name a compiled string symbol. Emit a pointer to a permanent heap copy; returns
 * 0 (nothing written) when the command has no literal. Only JSON-only menus have
 * literals, and MenuJson_Load builds each of those exactly once. */
static int wfield_literal(MWriter *w, const char *j, int jl)
{
    const char *tp; int tl;
    if (mjson_find(j, jl, "$.literal", &tp, &tl) != MJSON_TOK_STRING)
        return 0;
    char *text = (char *)malloc((size_t)tl);   /* decoded length < token length (quotes) */
    if (!text || mjson_get_string(j, jl, "$.literal", text, tl) < 0) {
        fprintf(stderr, "[menu-json] cannot decode literal %.*s\n", tl, tp);
        free(text);
        wptr(w, NULL);
        return 1;
    }
    wptr(w, text);
    return 1;
}

/* ---- opcode-name -> number (MENU_CMD_*; see UnpackMenu cases 0..22) ---- */
static int op_num(const char *op)
{
    if (!strcmp(op, "invisible"))      return 1;
    if (!strcmp(op, "nav"))            return 2;
    if (!strcmp(op, "bg_func"))        return 3;
    if (!strcmp(op, "colors"))         return 4;
    if (!strcmp(op, "sprite_bg"))      return 5;
    if (!strcmp(op, "invalid"))        return 6;
    if (!strcmp(op, "fg_func"))        return 7;
    if (!strcmp(op, "text"))           return 8;
    if (!strcmp(op, "sprite_fg"))      return 9;
    if (!strcmp(op, "string_table"))   return 10;
    if (!strcmp(op, "multiline"))      return 11;
    if (!strcmp(op, "number"))         return 12;
    if (!strcmp(op, "onselect_fire"))  return 13;
    if (!strcmp(op, "input_func"))     return 14;
    if (!strcmp(op, "before_draw"))    return 15;
    if (!strcmp(op, "after_draw"))     return 16;
    if (!strcmp(op, "up_skip"))        return 17;   /* MENU_CMD_UP_SKIP    = 0x11 */
    if (!strcmp(op, "down_skip"))      return 18;   /* MENU_CMD_DOWN_SKIP  = 0x12 */
    if (!strcmp(op, "left_skip"))      return 19;   /* MENU_CMD_LEFT_SKIP  = 0x13 */
    if (!strcmp(op, "right_skip"))     return 20;   /* MENU_CMD_RIGHT_SKIP = 0x14 */
    if (!strcmp(op, "sprite_copy"))    return 21;
    if (!strcmp(op, "text_no_ptr"))    return 22;
    return -1;
}

/* Emit one command (op word + its payload). cmd/cmdl is the command object slice. */
static void emit_command(MWriter *w, const char *cmd, int cmdl, const char *menuName)
{
    char op[24];
    if (mjson_get_string(cmd, cmdl, "$.op", op, (int)sizeof op) <= 0) return;
    int n = op_num(op);
    if (n < 0) { fprintf(stderr, "[menu-json] %s: unknown op '%s'\n", menuName, op); return; }
    w16(w, (unsigned)n);
    switch (n) {
    case 1:  /* invisible — no payload */
        break;
    case 2:  /* nav: i8 left,right,up,down */
        w8(w, (unsigned)mj_long_def(cmd, cmdl, "$.left",  0));
        w8(w, (unsigned)mj_long_def(cmd, cmdl, "$.right", 0));
        w8(w, (unsigned)mj_long_def(cmd, cmdl, "$.up",    0));
        w8(w, (unsigned)mj_long_def(cmd, cmdl, "$.down",  0));
        break;
    case 3:  /* bg_func: ptr */
        wfield_ptr(w, cmd, cmdl, "$.func", menuName, 0, 1);
        break;
    case 4: {  /* colors: u16 — hi nibble = frame color, lo nibble = background color */
        long val;
        if (mj_long(cmd, cmdl, "$.value", &val)) {        /* back-compat: packed key */
            w16(w, (unsigned)val);
        } else {
            long fr = mj_long_def(cmd, cmdl, "$.frame_color", 0);
            long bg = mj_long_def(cmd, cmdl, "$.background_color", 0);
            w16(w, (unsigned)(((fr & 0x0F) << 4) | (bg & 0x0F)));
        }
        break;
    }
    case 5:  /* sprite_bg: u16 */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.index", mj_long_def(cmd, cmdl, "$.value", 0)));
        break;
    case 6:  /* invalid: u16 */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.value", 0));
        break;
    case 7:  /* fg_func: ptr */
        wfield_ptr(w, cmd, cmdl, "$.func", menuName, 0, 1);
        break;
    case 8:  /* text: u16 flags, ptr str (a symbol, or SWOS 2020's inline "literal") */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.flags", 0));
        if (!wfield_literal(w, cmd, cmdl))
            wfield_ptr(w, cmd, cmdl, "$.str", menuName, 1, 0);
        break;
    case 9:  /* sprite_fg: u16 index */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.index", 0));
        break;
    case 10: /* string_table: u16 flags, ptr table */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.flags", 0));
        wfield_ptr(w, cmd, cmdl, "$.table", menuName, 0, 0);
        break;
    case 11: /* multiline: u16 flags, ptr str (a symbol, or an inline "literal") */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.flags", 0));
        if (!wfield_literal(w, cmd, cmdl))
            wfield_ptr(w, cmd, cmdl, "$.str", menuName, 1, 0);
        break;
    case 12: /* number: u16 flags, u16 value */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.flags", 0));
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.value", 0));
        break;
    case 13: /* onselect_fire: ptr */
        wfield_ptr(w, cmd, cmdl, "$.func", menuName, 0, 1);
        break;
    case 14: /* input_func: u16 mask, ptr */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.mask", 0));
        wfield_ptr(w, cmd, cmdl, "$.func", menuName, 0, 1);
        break;
    case 15: /* before_draw: ptr */
        wfield_ptr(w, cmd, cmdl, "$.func", menuName, 0, 1);
        break;
    case 16: /* after_draw: ptr */
        wfield_ptr(w, cmd, cmdl, "$.func", menuName, 0, 1);
        break;
    case 17: case 18: case 19: case 20: /* *_skip: u8 skip, u8 dir */
        w8(w, (unsigned)mj_long_def(cmd, cmdl, "$.skip", 0));
        w8(w, (unsigned)mj_long_def(cmd, cmdl, "$.dir",  0));
        break;
    case 21: /* sprite_copy: ptr */
        wfield_ptr(w, cmd, cmdl, "$.sprite", menuName, 0, 0);
        break;
    case 22: /* text_no_ptr: u16 flags (deferred string; UnpackMenu sets fg=0xFFFFFFFF) */
        w16(w, (unsigned)mj_long_def(cmd, cmdl, "$.flags", 0));
        break;
    default:
        break;
    }
}

/* Emit one entry's coords (current byte/word mode) + its command list + the
 * MENU_CMD_END (opcode 0) terminator. `it`/`itl` is the entry object slice. */
static int is_stream_marker(unsigned word)
{
    return word == MK_COORDS_BYTE || word == MK_COORDS_WIDE || word == MENU_FLAGS_MARKER ||
           (word >= MK_END && word <= MK_RESET_TMPL);
}

static void emit_entry(MWriter *w, const char *it, int itl, int byteMode, const char *menuName)
{
    long x = mj_long_def(it, itl, "$.coords.x", 0);
    long y = mj_long_def(it, itl, "$.coords.y", 0);
    long cw = mj_long_def(it, itl, "$.coords.w", 0);
    long ch = mj_long_def(it, itl, "$.coords.h", 0);

    /* UnpackMenu tells a marker from an entry by the entry's first word, so an entry
     * whose coordinates START with a marker's bytes would be misread -- and SWOS 2020
     * places entries at x = -1, which is 0xFFFF, the byte-coords marker. DOS never had
     * byte coords, so there it was legal. Restate the current coords mode first:
     * UnpackMenu consumes that marker and then reads the coordinates without looking.
     * No compiled menu has such an entry (it would never have unpacked), so their
     * bytes are unchanged. */
    unsigned first = byteMode ? (unsigned)((x & 0xFF) | ((y & 0xFF) << 8))
                              : (unsigned)(x & 0xFFFF);     /* as w16/w8 lay it down */
    if (is_stream_marker(first))
        w16(w, byteMode ? MK_COORDS_BYTE : MK_COORDS_WIDE);
    if (byteMode) {
        w8(w, (unsigned)(x & 0xFF));  w8(w, (unsigned)(y & 0xFF));
        w8(w, (unsigned)(cw & 0xFF)); w8(w, (unsigned)(ch & 0xFF));
    } else {
        w16(w, (unsigned)(x & 0xFFFF));  w16(w, (unsigned)(y & 0xFFFF));
        w16(w, (unsigned)(cw & 0xFFFF)); w16(w, (unsigned)(ch & 0xFFFF));
    }
    const char *cmds; int cmdsl;
    if (mjson_find(it, itl, "$.commands", &cmds, &cmdsl) == MJSON_TOK_ARRAY) {
        int off = 0, ko, kl, vo, vl, vt;
        while ((off = mjson_next(cmds, cmdsl, off, &ko, &kl, &vo, &vl, &vt)) != 0) {
            if (vt != MJSON_TOK_OBJECT) continue;
            emit_command(w, cmds + vo, vl, menuName);
        }
    }
    w16(w, 0);   /* MENU_CMD_END */
}

/* ======================================================================
 * MenuJson_BuildPacked — JSON object -> packed §0 byte-stream.
 * Returns the byte length (incl. the trailing end marker), or -1 on error.
 * ====================================================================== */
int MenuJson_BuildPacked(const char *j, int jl, uint8_t *out, int outcap)
{
    MWriter w = { out, out + outcap, 0 };
    char menuName[64];
    if (mjson_get_string(j, jl, "$.name", menuName, (int)sizeof menuName) <= 0)
        menuName[0] = '\0';

    /* Header: onInit, onReturn, onDraw (native ptr), then u32 defaultEntry. */
    wptr(&w, mj_sym(j, jl, "$.onInit",   menuName, 1));
    wptr(&w, mj_sym(j, jl, "$.onReturn", menuName, 1));
    wptr(&w, mj_sym(j, jl, "$.onDraw",   menuName, 1));
    w32(&w, (uint32_t)mj_long_def(j, jl, "$.defaultEntry", 0));

    /* OpenSWOS extension (docs/MENUS.md §0a): the flags word goes straight after
     * the header, the only place UnpackMenu looks for it. None of the compiled
     * menus has one, so their bytes are unchanged. */
    if (mj_bool_def(j, jl, "$.menu_flags.dos_redraw", 0)) {
        w16(&w, MENU_FLAGS_MARKER);
        w16(&w, MENU_FLAG_DOS_REDRAW);
    }

    const char *items; int itemsl;
    if (mjson_find(j, jl, "$.items", &items, &itemsl) != MJSON_TOK_ARRAY)
        return -1;

    int byteMode = 0;       /* 0 = word coords (default), persists like UnpackMenu */
    int sawEnd = 0;
    int off = 0, ko, kl, vo, vl, vt;
    while ((off = mjson_next(items, itemsl, off, &ko, &kl, &vo, &vl, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) continue;
        const char *it = items + vo; int itl = vl;
        char kind[24];
        if (mjson_get_string(it, itl, "$.kind", kind, (int)sizeof kind) <= 0) continue;

        if (!strcmp(kind, "entry")) {
            emit_entry(&w, it, itl, byteMode, menuName);
        } else if (!strcmp(kind, "set_origin")) {
            w16(&w, MK_SET_ORIGIN);
            w16(&w, (unsigned)(mj_long_def(it, itl, "$.x", 0) & 0xFFFF));
            w16(&w, (unsigned)(mj_long_def(it, itl, "$.y", 0) & 0xFFFF));
        } else if (!strcmp(kind, "coords_mode")) {
            int wide = mj_bool_def(it, itl, "$.wide", 1);
            byteMode = !wide;
            w16(&w, wide ? MK_COORDS_WIDE : MK_COORDS_BYTE);
        } else if (!strcmp(kind, "start_template")) {
            w16(&w, MK_TEMPLATE);
        } else if (!strcmp(kind, "reset_template")) {
            w16(&w, MK_RESET_TMPL);
        } else if (!strcmp(kind, "end")) {
            w16(&w, MK_END);
            sawEnd = 1;
            break;
        } else {
            fprintf(stderr, "[menu-json] %s: unknown item kind '%s'\n", menuName, kind);
        }
    }
    if (!sawEnd)
        w16(&w, MK_END);     /* defensively terminate if the JSON omitted it */

    if (w.overflow) return -1;
    return (int)(w.p - out);
}

/* ======================================================================
 * Verify: build every menu from JSON and compare against the compiled static
 * bytes (MENUS.md §2d/§3b). Pointer slots match exactly because menu_resolve
 * returns the same link-time addresses the static initializers baked in.
 * ====================================================================== */

/* --- the 52 compiled static menus, by name (still authoritative until the
 *     round-trip is proven green and menu_data.c is retired) --- */
extern uint8_t _buyOtherPlayersMenu[];       extern uint8_t _buyPlayerMenu[];             extern uint8_t _careerMenu[];
extern uint8_t _careerOverMenu[];            extern uint8_t _chairmanMemosMenu[];         extern uint8_t _chooseTacticsMenu[];
extern uint8_t _chooseTeamsFriendlyMenu[];   extern uint8_t _chooseTeamsFriendlyMenu2[];  extern uint8_t _chooseTeamsMenu[];
extern uint8_t _clubBusinessMenu[];          extern uint8_t _competitionStatsMenu[];      extern uint8_t _competitionTopGoalScorersMenu[];
extern uint8_t _contestMenu[];               extern uint8_t _continueAbortMenu[];         extern uint8_t _continueMenu[];
extern uint8_t _designDiyCupMenu[];          extern uint8_t _designDiyLeagueMenu[];       extern uint8_t _designDiyTournamentMenu[];
extern uint8_t _diyCupMenu[];                extern uint8_t _diyLeagueTournamentMenu[];   extern uint8_t _editMenu[];
extern uint8_t _editTacticsMenu[];
extern uint8_t _editTeamsMenu[];             extern uint8_t _friendlyGameMenu[];          extern uint8_t _gotJobOfferMenu[];
extern uint8_t _highlightsMenu[];            extern uint8_t _importTacticsMenu[];         extern uint8_t _jobOffersMenu[];
extern uint8_t _managementRecordMenu[];      extern uint8_t _nationalTeamAddPlayerMenu[]; extern uint8_t _offersMenu[];
extern uint8_t _optionsMenu[];               extern uint8_t _playMatchMenu[];             extern uint8_t _playerOffersMenu[];
extern uint8_t _playerTransferMenu[];        extern uint8_t _practiceMenu[];              extern uint8_t _selectFilesMenu[];
extern uint8_t _selectNationalTeamMenu[];
extern uint8_t _selectTeamsForTacticsMenu[]; extern uint8_t _selectTeamsMenu[];           extern uint8_t _shootingPracticeSetupMenu[];
extern uint8_t _shootoutOrderMenu[];         extern uint8_t _showPlayerMarketMenu[];
extern uint8_t _squadMenu[];                 extern uint8_t _stadiumMenu[];               extern uint8_t _swosMainMenu[];
extern uint8_t _thirdPartyOfferMenu[];       extern uint8_t _topGoalScorersMenu[];        extern uint8_t _transferMarketMenu[];
extern uint8_t _viewCompetitionsMenu[];      extern uint8_t _viewSquadMenu[];

extern uint8_t _challengeMenu[];
static const struct { const char *name; uint8_t *menu; } s_staticMenus[] = {
    { "challengeMenu", _challengeMenu },
    { "buyOtherPlayersMenu", _buyOtherPlayersMenu },
    { "buyPlayerMenu", _buyPlayerMenu },
    { "careerMenu", _careerMenu },
    { "careerOverMenu", _careerOverMenu },
    { "chairmanMemosMenu", _chairmanMemosMenu },
    { "chooseTacticsMenu", _chooseTacticsMenu },
    { "chooseTeamsFriendlyMenu", _chooseTeamsFriendlyMenu },
    { "chooseTeamsFriendlyMenu2", _chooseTeamsFriendlyMenu2 },
    { "chooseTeamsMenu", _chooseTeamsMenu },
    { "clubBusinessMenu", _clubBusinessMenu },
    { "competitionStatsMenu", _competitionStatsMenu },
    { "competitionTopGoalScorersMenu", _competitionTopGoalScorersMenu },
    { "contestMenu", _contestMenu },
    { "continueAbortMenu", _continueAbortMenu },
    { "continueMenu", _continueMenu },
    { "designDiyCupMenu", _designDiyCupMenu },
    { "designDiyLeagueMenu", _designDiyLeagueMenu },
    { "designDiyTournamentMenu", _designDiyTournamentMenu },
    { "diyCupMenu", _diyCupMenu },
    { "diyLeagueTournamentMenu", _diyLeagueTournamentMenu },
    { "editMenu", _editMenu },           /* OpenSWOS: the main menu's EDIT submenu */
    { "editTacticsMenu", _editTacticsMenu },
    { "editTeamsMenu", _editTeamsMenu },
    { "friendlyGameMenu", _friendlyGameMenu },
    { "gotJobOfferMenu", _gotJobOfferMenu },
    { "highlightsMenu", _highlightsMenu },
    { "importTacticsMenu", _importTacticsMenu },
    { "jobOffersMenu", _jobOffersMenu },
    { "managementRecordMenu", _managementRecordMenu },
    { "nationalTeamAddPlayerMenu", _nationalTeamAddPlayerMenu },
    { "offersMenu", _offersMenu },
    { "optionsMenu", _optionsMenu },
    { "playMatchMenu", _playMatchMenu },
    { "playerOffersMenu", _playerOffersMenu },
    { "playerTransferMenu", _playerTransferMenu },
    { "practiceMenu", _practiceMenu },   /* OpenSWOS: the main menu's PRACTICE submenu */
    { "selectFilesMenu", _selectFilesMenu },
    { "selectNationalTeamMenu", _selectNationalTeamMenu },
    { "selectTeamsForTacticsMenu", _selectTeamsForTacticsMenu },
    { "selectTeamsMenu", _selectTeamsMenu },
    { "shootingPracticeSetupMenu", _shootingPracticeSetupMenu },   /* OpenSWOS: SHOOTING PRACTICE's setup screen */
    { "shootoutOrderMenu", _shootoutOrderMenu },   /* OpenSWOS: PENALTY SHOOTOUT's shooting-order screen */
    { "showPlayerMarketMenu", _showPlayerMarketMenu },
    { "squadMenu", _squadMenu },
    { "stadiumMenu", _stadiumMenu },
    { "swosMainMenu", _swosMainMenu },
    { "thirdPartyOfferMenu", _thirdPartyOfferMenu },
    { "topGoalScorersMenu", _topGoalScorersMenu },
    { "transferMarketMenu", _transferMarketMenu },
    { "viewCompetitionsMenu", _viewCompetitionsMenu },
    { "viewSquadMenu", _viewSquadMenu },
};
#define NUM_STATIC_MENUS ((int)(sizeof s_staticMenus / sizeof s_staticMenus[0]))

static uint8_t *menu_slurp(const char *path, int *out_len)
{
    /* Anchored at the data root — rom:/ on the Switch (swos_io.h). */
    char anchored[1024];
    FILE *f = fopen(swos_data_path(path, anchored, (uint32_t)sizeof anchored), "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    *out_len = (int)n;
    return buf;
}

/* Build menu `name` from JSON and memcmp vs its compiled static bytes.
 * Returns 0 = identical, 1 = differs, -1 = error. */
static int menu_verify_one(const char *name, const uint8_t *staticMenu)
{
    char path[160];
    snprintf(path, sizeof path, "data_json/menus/%s.json", name);
    int jl = 0;
    uint8_t *j = menu_slurp(path, &jl);
    if (!j) { fprintf(stderr, "[menu-json] %s: cannot open %s\n", name, path); return -1; }

    static uint8_t packed[16384];   /* largest static menu is well under this */
    int len = MenuJson_BuildPacked((const char *)j, jl, packed, (int)sizeof packed);
    free(j);
    if (len < 0) { fprintf(stderr, "[menu-json] %s: build failed (overflow/parse)\n", name); return -1; }

    if (memcmp(packed, staticMenu, (size_t)len) != 0) {
        int off;
        for (off = 0; off < len; off++)
            if (packed[off] != staticMenu[off]) break;
        fprintf(stderr, "[menu-json] %-30s DIFF at +%d (json=%02X static=%02X) len=%d\n",
                name, off, packed[off], staticMenu[off], len);
        int lo = off - 12; if (lo < 0) lo = 0;
        fprintf(stderr, "   json  :");
        for (int k = lo; k < off + 12 && k < len; k++) fprintf(stderr, " %02X", packed[k]);
        fprintf(stderr, "\n   static:");
        for (int k = lo; k < off + 12 && k < len; k++) fprintf(stderr, " %02X", staticMenu[k]);
        fprintf(stderr, "\n");
        return 1;
    }
    return 0;
}

/* Public entry: verify every corpus menu round-trips to the static bytes.
 * Returns the number of mismatches (0 = all green). Gated by the caller on env. */
int MenuJson_VerifyAll(void)
{
    int pass = 0, diff = 0, err = 0;
    s_menu_verify_raw = 1;   /* compare against the untranslated static bytes (raw symbols) */
    for (int i = 0; i < NUM_STATIC_MENUS; i++) {
        int rc = menu_verify_one(s_staticMenus[i].name, s_staticMenus[i].menu);
        if (rc == 0) pass++;
        else if (rc == 1) diff++;
        else err++;
    }
    s_menu_verify_raw = 0;
    fprintf(stderr, "[menu-json] verify: %d/%d byte-identical, %d differ, %d error\n",
            pass, NUM_STATIC_MENUS, diff, err);
    return diff + err;
}

/* Build every menu from JSON and overwrite the compiled static `fooMenu` storage
 * IN PLACE, so editing the per-menu JSON changes the running game with no
 * call-site churn (every `ShowMenu(fooMenu)` keeps reading the same symbol).
 * The overwrite is bounded by the menu's compiled packed length (packed_source_len)
 * so an edit can never spill past the static struct into adjacent globals — a menu
 * that grows beyond its compiled size is left compiled and flagged (growing a menu
 * needs the call-site-repoint step, MENUS.md §1e). Returns the number skipped+errored. */
int MenuJson_InstallAll(void)
{
    /* All menu-json logging goes to stderr (freopen'd to bin/SWOS.log by main.c).
     * stdout is block-buffered and would not flush while the game is running. */
    fprintf(stderr, "[menu-json] SWOS_MENU_JSON set — installing menus from "
                    "data_json/menus/*.json (relative to the binary dir)\n");
    /* Byte-exact round-trip check (JSON-built vs compiled static menus), before the
     * install loop overwrites them. Opt-in via SWOS_MENU_JSON_VERIFY. */
    if (getenv("SWOS_MENU_JSON_VERIFY")) MenuJson_VerifyAll();
    int installed = 0, changed = 0, toobig = 0, err = 0;
    for (int i = 0; i < NUM_STATIC_MENUS; i++) {
        const char *name = s_staticMenus[i].name;
        uint8_t *dst = s_staticMenus[i].menu;
        char path[160];
        snprintf(path, sizeof path, "data_json/menus/%s.json", name);
        int jl = 0;
        uint8_t *j = menu_slurp(path, &jl);
        if (!j) { fprintf(stderr, "[menu-json] %s: cannot open %s\n", name, path); err++; continue; }

        static uint8_t packed[16384];
        int len = MenuJson_BuildPacked((const char *)j, jl, packed, (int)sizeof packed);
        if (len < 0) { free(j); fprintf(stderr, "[menu-json] %s: build failed\n", name); err++; continue; }

        int cap = packed_source_len(dst);
        if (len > cap) {
            free(j);
            fprintf(stderr, "[menu-json] %s: SKIP — JSON build %d B > compiled %d B "
                    "(growing a menu needs the call-site repoint, MENUS.md §1e)\n", name, len, cap);
            toobig++;
            continue;
        }
        /* "Edited" = the JSON STRUCTURALLY differs from the compiled menu. Compare a
         * raw-symbol build (no tr) so translated display text isn't mistaken for an edit;
         * the bytes installed below keep the tr() pointers for i18n. */
        static uint8_t rawpacked[16384];
        s_menu_verify_raw = 1;
        int rawlen = MenuJson_BuildPacked((const char *)j, jl, rawpacked, (int)sizeof rawpacked);
        s_menu_verify_raw = 0;
        free(j);
        int diff = (rawlen < 0) || (rawlen != cap) || memcmp(rawpacked, dst, (size_t)rawlen) != 0;
        memcpy(dst, packed, (size_t)len);   /* JSON is now authoritative for this menu */
        installed++;
        if (diff) {
            changed++;
            fprintf(stderr, "[menu-json]   %s: loaded from JSON (EDITED vs compiled)\n", name);
        }
    }
    fprintf(stderr, "[menu-json] install: %d/%d menus loaded from JSON in place "
            "(%d edited vs compiled, %d too-large-skipped, %d error)\n",
            installed, NUM_STATIC_MENUS, changed, toobig, err);
    fflush(stderr);     /* SWOS.log is fully buffered once freopen'd to a file */
    return toobig + err;
}

/* ======================================================================
 * Menus with no compiled counterpart (docs/MENUS.md §10) -- the imported
 * SWOS 2020 menus. Each is built from JSON on first use into its own heap buffer
 * and cached for the life of the process, so the pointer handed to ShowMenu stays
 * valid for RestorePreviousMenu to unpack again later.
 * ====================================================================== */
#define MAX_DYNAMIC_MENUS 64
static struct { char name[64]; uint8_t *packed; } s_dynamicMenus[MAX_DYNAMIC_MENUS];
static int s_numDynamicMenus;

/* `name` is the path under data_json/menus/ without ".json", e.g. "swos2020/logInMenu".
 * Returns the packed menu for ShowMenu, or NULL (logged) if it cannot be built. */
uint8_t *MenuJson_Load(const char *name)
{
    for (int i = 0; i < s_numDynamicMenus; i++)
        if (strcmp(s_dynamicMenus[i].name, name) == 0)
            return s_dynamicMenus[i].packed;

    if (s_numDynamicMenus == MAX_DYNAMIC_MENUS) {
        fprintf(stderr, "[menu-json] Load %s: all %d dynamic menu slots used\n", name, MAX_DYNAMIC_MENUS);
        return NULL;
    }
    if (strlen(name) >= sizeof s_dynamicMenus[0].name) {
        fprintf(stderr, "[menu-json] Load: name too long: %s\n", name);
        return NULL;
    }

    char path[160];
    snprintf(path, sizeof path, "data_json/menus/%s.json", name);
    int jl = 0;
    uint8_t *j = menu_slurp(path, &jl);
    if (!j) {
        fprintf(stderr, "[menu-json] Load %s: cannot open %s\n", name, path);
        return NULL;
    }

    static uint8_t packed[32768];
    s_dynamicBuild = 1;
    int len = MenuJson_BuildPacked((const char *)j, jl, packed, (int)sizeof packed);
    s_dynamicBuild = 0;
    free(j);
    if (len < 0) {
        fprintf(stderr, "[menu-json] Load %s: build failed (overflow/parse)\n", name);
        return NULL;
    }

    uint8_t *copy = (uint8_t *)malloc((size_t)len);
    if (!copy) {
        fprintf(stderr, "[menu-json] Load %s: out of memory (%d B)\n", name, len);
        return NULL;
    }
    memcpy(copy, packed, (size_t)len);
    snprintf(s_dynamicMenus[s_numDynamicMenus].name, sizeof s_dynamicMenus[0].name, "%s", name);
    s_dynamicMenus[s_numDynamicMenus].packed = copy;
    s_numDynamicMenus++;
    fprintf(stderr, "[menu-json] loaded %s (%d B packed)\n", name, len);
    return copy;
}

/* Unpack a JSON-only menu into scratch memory -- never the live _currentMenu -- and
 * check what UnpackMenu made of it against the JSON it came from: the entry count,
 * and for every entry its ordinal, screen rectangle, nav links and text (a literal's
 * characters, or a deferred buffer inside the unpacked menu). This runs the new
 * stream features -- the flags word, literals -- through the real unpacker.
 * Returns the number of mismatches, or -1 if the menu would not load. */
int MenuJson_VerifyDynamic(const char *name, int expectedEntries)
{
    uint8_t *packed = MenuJson_Load(name);
    if (!packed)
        return -1;

    char path[160];
    snprintf(path, sizeof path, "data_json/menus/%s.json", name);
    int jl = 0;
    uint8_t *j = menu_slurp(path, &jl);
    if (!j)
        return -1;

    static uint8_t scratch[8 * 1024 + 80 * 1024];   /* the size of _currentMenu */
    UnpackMenu(packed, scratch);
    Menu *menu = (Menu *)scratch;
    int bad = 0;
#define VFAIL(...) do { if (bad++ < 8) { fprintf(stderr, "[menu-json] verify %s: ", name); \
                                          fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

    if (menu->numEntries != expectedEntries)
        VFAIL("%u entries unpacked, header says %d", (unsigned)menu->numEntries, expectedEntries);

    const char *items; int itemsl;
    if (mjson_find((const char *)j, jl, "$.items", &items, &itemsl) != MJSON_TOK_ARRAY) {
        free(j);
        VFAIL("no items array");
        return bad;
    }

    int ordinal = 0, originX = 0, originY = 0, template = 0, deferred = 0;
    int off = 0, ko, kl, vo, vl, vt;
    while ((off = mjson_next(items, itemsl, off, &ko, &kl, &vo, &vl, &vt)) != 0) {
        if (vt != MJSON_TOK_OBJECT) continue;
        const char *it = items + vo; int itl = vl;
        char kind[24];
        if (mjson_get_string(it, itl, "$.kind", kind, (int)sizeof kind) <= 0) continue;
        if (!strcmp(kind, "set_origin")) {
            originX = (int)mj_long_def(it, itl, "$.x", 0);
            originY = (int)mj_long_def(it, itl, "$.y", 0);
            continue;
        }
        if (!strcmp(kind, "start_template")) { template = 1; continue; }
        if (strcmp(kind, "entry") != 0) continue;
        if (template) { template = 0; continue; }       /* fills the template, not an ordinal */

        if (ordinal >= menu->numEntries) {
            VFAIL("JSON has more entries than the %u unpacked", (unsigned)menu->numEntries);
            break;
        }
        MenuEntry *e = (MenuEntry *)(scratch + MENU_HEADER_SIZE + (size_t)ordinal * MENUENTRY_STRIDE);
        uint16_t x = (uint16_t)(mj_long_def(it, itl, "$.coords.x", 0) + 24 + originX);
        uint16_t y = (uint16_t)(mj_long_def(it, itl, "$.coords.y", 0) + 36 + originY);
        uint16_t w = (uint16_t)mj_long_def(it, itl, "$.coords.w", 0);
        uint16_t h = (uint16_t)mj_long_def(it, itl, "$.coords.h", 0);
        if (e->ordinal != ordinal)
            VFAIL("entry %d: ordinal %u", ordinal, (unsigned)e->ordinal);
        if (e->x != x || e->y != y || e->dwWidth != w || e->dwHeight != h)
            VFAIL("entry %d: rect %u,%u %ux%u, expected %u,%u %ux%u", ordinal,
                  e->x, e->y, e->dwWidth, e->dwHeight, x, y, w, h);

        const char *cmds; int cmdsl;
        if (mjson_find(it, itl, "$.commands", &cmds, &cmdsl) == MJSON_TOK_ARRAY) {
            int co = 0, cko, ckl, cvo, cvl, cvt;
            while ((co = mjson_next(cmds, cmdsl, co, &cko, &ckl, &cvo, &cvl, &cvt)) != 0) {
                if (cvt != MJSON_TOK_OBJECT) continue;
                const char *c = cmds + cvo; int cl = cvl;
                char op[24], literal[256];
                if (mjson_get_string(c, cl, "$.op", op, (int)sizeof op) <= 0) continue;
                if (!strcmp(op, "nav")) {
                    if (e->leftEntry != (int8_t)mj_long_def(c, cl, "$.left", 0) ||
                        e->rightEntry != (int8_t)mj_long_def(c, cl, "$.right", 0) ||
                        e->upEntry != (int8_t)mj_long_def(c, cl, "$.up", 0) ||
                        e->downEntry != (int8_t)mj_long_def(c, cl, "$.down", 0))
                        VFAIL("entry %d: nav %d,%d,%d,%d", ordinal,
                              e->leftEntry, e->rightEntry, e->upEntry, e->downEntry);
                } else if (!strcmp(op, "text_no_ptr")) {
                    deferred++;
                    if (e->content != ENTRY_STRING || e->fg < (uintptr_t)scratch ||
                        e->fg >= (uintptr_t)scratch + sizeof scratch || *(char *)e->fg != 0)
                        VFAIL("entry %d: deferred text not given an empty buffer in the menu", ordinal);
                } else if (!strcmp(op, "text") &&
                           mjson_get_string(c, cl, "$.literal", literal, (int)sizeof literal) >= 0) {
                    if (e->content != ENTRY_STRING || !e->fg || strcmp((char *)e->fg, literal) != 0)
                        VFAIL("entry %d: literal \"%s\" not unpacked", ordinal, literal);
                }
            }
        }
        ordinal++;
    }
    if (ordinal != menu->numEntries)
        VFAIL("%d JSON entries, %u unpacked", ordinal, (unsigned)menu->numEntries);

    /* Everything UnpackMenu wrote must fit the real _currentMenu. */
    size_t used = MENU_HEADER_SIZE + (size_t)menu->numEntries * MENUENTRY_STRIDE + (size_t)deferred * 0x46;
    if (used > sizeof scratch)
        VFAIL("unpacks to %zu B, _currentMenu holds %zu", used, sizeof scratch);
#undef VFAIL
    free(j);
    return bad;
}
