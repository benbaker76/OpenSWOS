/* tileagent.c - see tileagent.h.
 *
 * The rules implemented here are the ones in docs/FORMAT.md, and the editor's
 * src/core/player.cpp and anim.cpp implement them too. The two are compared
 * frame by frame in tests/runtime_tests.cpp; change one and you change both.
 *
 * Arithmetic: 32-bit throughout, with 32x32->64 multiplies and shifts where a
 * product needs the room. There is no 64-bit division (a 68020 cannot do one
 * without a library call) and no floating point.
 */
#include "tileagent.h"

#include <string.h>

#ifndef TA_NO_STDLIB
#include <stdlib.h>
#endif

/* ================================================================== memory */

#ifndef TA_NO_STDLIB
static void *ta_default_alloc(void *user, size_t size)
{
    (void)user;
    return malloc(size);
}
static void ta_default_release(void *user, void *pointer)
{
    (void)user;
    free(pointer);
}
#endif

static int ta_allocator_from(const ta_allocator *given, ta_allocator *out)
{
    if (given && given->alloc && given->release) {
        *out = *given;
        return 1;
    }
#ifndef TA_NO_STDLIB
    out->alloc = ta_default_alloc;
    out->release = ta_default_release;
    out->user = NULL;
    return 1;
#else
    return 0;
#endif
}

/* An arena: everything a map owns is released in one go. */
typedef struct ta_block {
    struct ta_block *next;
    size_t used, size;
} ta_block;

typedef struct ta_arena {
    ta_allocator allocator;
    ta_block *head;
    int failed;
} ta_arena;

#define TA_BLOCK_SIZE 16384u
#define TA_ALIGN 8u

static void *arena_alloc(ta_arena *arena, size_t size)
{
    ta_block *block = arena->head;
    size_t header = (sizeof(ta_block) + TA_ALIGN - 1) & ~(size_t)(TA_ALIGN - 1);
    void *result;

    if (size == 0)
        size = 1;
    size = (size + TA_ALIGN - 1) & ~(size_t)(TA_ALIGN - 1);
    if (!block || block->size - block->used < size) {
        size_t wanted = size > TA_BLOCK_SIZE ? size : TA_BLOCK_SIZE;
        block = (ta_block *)arena->allocator.alloc(arena->allocator.user, header + wanted);
        if (!block) {
            arena->failed = 1;
            return NULL;
        }
        block->next = arena->head;
        block->used = 0;
        block->size = wanted;
        arena->head = block;
    }
    result = (uint8_t *)block + header + block->used;
    block->used += size;
    memset(result, 0, size);
    return result;
}

static void arena_free(ta_arena *arena)
{
    ta_block *block = arena->head;
    while (block) {
        ta_block *next = block->next;
        arena->allocator.release(arena->allocator.user, block);
        block = next;
    }
    arena->head = NULL;
}

static const char *arena_string(ta_arena *arena, const char *text, size_t length)
{
    char *copy = (char *)arena_alloc(arena, length + 1);
    if (!copy)
        return "";
    memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

static void set_error(char *error, size_t size, const char *message, const char *detail)
{
    size_t at = 0;
    if (!error || size == 0)
        return;
    while (*message && at + 1 < size)
        error[at++] = *message++;
    while (detail && *detail && at + 1 < size)
        error[at++] = *detail++;
    error[at] = 0;
}

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

/* ================================================================== easing */

#ifndef TA_EASE

static const int32_t k_sin_quarter[65] = {
         0,   1608,   3216,   4821,   6424,   8022,   9616,  11204,
     12785,  14359,  15924,  17479,  19024,  20557,  22078,  23586,
     25080,  26558,  28020,  29466,  30893,  32303,  33692,  35062,
     36410,  37736,  39040,  40320,  41576,  42806,  44011,  45190,
     46341,  47464,  48559,  49624,  50660,  51665,  52639,  53581,
     54491,  55368,  56212,  57022,  57798,  58538,  59244,  59914,
     60547,  61145,  61705,  62228,  62714,  63162,  63572,  63944,
     64277,  64571,  64827,  65043,  65220,  65358,  65457,  65516,
     65536,
};

static const int32_t k_exp2_frac[17] = {
     65536,  68438,  71468,  74632,  77936,  81386,
     84990,  88752,  92682,  96785, 101070, 105545,
    110218, 115098, 120194, 125515, 131072,
};

#define EASE_BACK_S 111515
#define EASE_BACK_S2 170060
#define EASE_BOUNCE_N 495616
#define EASE_B_1D 23831
#define EASE_B_2D 47663
#define EASE_B_25D 59578
#define EASE_B_15D 35747
#define EASE_B_225D 53620
#define EASE_B_2625D 62557
#define EASE_B_075 49152
#define EASE_B_09375 61440
#define EASE_B_0984375 64512

static int32_t fmul(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
}

static int32_t sin_turns(int32_t turns)
{
    int32_t q, idx, frac, a, b, v;
    int32_t neg = 0;

    turns &= (TA_ONE - 1);
    if (turns >= TA_ONE / 2) {
        turns -= TA_ONE / 2;
        neg = 1;
    }
    if (turns >= TA_ONE / 4)
        turns = TA_ONE / 2 - turns;

    q = turns << 2;
    idx = q >> 10;
    frac = q & 1023;
    if (idx >= 64) {
        v = k_sin_quarter[64];
    } else {
        a = k_sin_quarter[idx];
        b = k_sin_quarter[idx + 1];
        v = a + (int32_t)(((int64_t)(b - a) * frac) >> 10);
    }
    return neg ? -v : v;
}

static int32_t cos_turns(int32_t turns)
{
    return sin_turns(turns + TA_ONE / 4);
}

static int32_t exp2_fix(int32_t x)
{
    int32_t whole = x >> 16;
    int32_t frac = x - (whole * 65536);
    int32_t idx = frac >> 12;
    int32_t rem = frac & 4095;
    int32_t a = k_exp2_frac[idx];
    int32_t b = k_exp2_frac[idx + 1];
    int32_t v = a + (int32_t)(((int64_t)(b - a) * rem) >> 12);

    if (whole > 0) {
        if (whole >= 15)
            return 0x7FFFFFFF;
        return v << whole;
    }
    if (whole < 0) {
        if (whole <= -31)
            return 0;
        return v >> (-whole);
    }
    return v;
}

static int32_t sqrt_fix(int32_t x)
{
    uint64_t rem = 0, root = 0, v;
    int i;

    if (x <= 0)
        return 0;
    v = (uint64_t)(uint32_t)x << 16;
    for (i = 0; i < 32; i++) {
        rem = (rem << 2) | (v >> 62);
        v <<= 2;
        root <<= 1;
        if (rem > root) {
            rem -= root | 1;
            root += 2;
        }
    }
    return (int32_t)(root >> 1);
}

static int32_t bounce_out(int32_t t)
{
    if (t < EASE_B_1D)
        return fmul(EASE_BOUNCE_N, fmul(t, t));
    if (t < EASE_B_2D) {
        t -= EASE_B_15D;
        return fmul(EASE_BOUNCE_N, fmul(t, t)) + EASE_B_075;
    }
    if (t < EASE_B_25D) {
        t -= EASE_B_225D;
        return fmul(EASE_BOUNCE_N, fmul(t, t)) + EASE_B_09375;
    }
    t -= EASE_B_2625D;
    return fmul(EASE_BOUNCE_N, fmul(t, t)) + EASE_B_0984375;
}

int32_t ta_ease(int kind, int32_t t)
{
    int32_t u, t2, u2, s;

    if (t <= 0)
        return 0;
    if (t >= TA_ONE)
        return TA_ONE;

    u = TA_ONE - t;
    t2 = fmul(t, t);
    u2 = fmul(u, u);

    switch (kind) {
    case 0:  return t;

    case 1:  return t2;
    case 2:  return TA_ONE - u2;
    case 3:  return (t < TA_ONE / 2) ? 2 * t2 : TA_ONE - 2 * u2;

    case 4:  return fmul(t2, t);
    case 5:  return TA_ONE - fmul(u2, u);
    case 6:  return (t < TA_ONE / 2) ? 4 * fmul(t2, t) : TA_ONE - 4 * fmul(u2, u);

    case 7:  return fmul(t2, t2);
    case 8:  return TA_ONE - fmul(u2, u2);
    case 9:  return (t < TA_ONE / 2) ? 8 * fmul(t2, t2) : TA_ONE - 8 * fmul(u2, u2);

    case 10: return fmul(fmul(t2, t2), t);
    case 11: return TA_ONE - fmul(fmul(u2, u2), u);
    case 12: return (t < TA_ONE / 2) ? 16 * fmul(fmul(t2, t2), t) : TA_ONE - 16 * fmul(fmul(u2, u2), u);

    case 13: return TA_ONE - cos_turns(t >> 2);
    case 14: return sin_turns(t >> 2);
    case 15: return -(cos_turns(t >> 1) - TA_ONE) >> 1;

    case 16: return exp2_fix(10 * t - 10 * TA_ONE);
    case 17: return TA_ONE - exp2_fix(-10 * t);
    case 18: return (t < TA_ONE / 2) ? exp2_fix(20 * t - 10 * TA_ONE) >> 1
                                     : (2 * TA_ONE - exp2_fix(-20 * t + 10 * TA_ONE)) >> 1;

    case 19: return TA_ONE - sqrt_fix(TA_ONE - t2);
    case 20: return sqrt_fix(TA_ONE - u2);
    case 21: return (t < TA_ONE / 2) ? (TA_ONE - sqrt_fix(TA_ONE - 4 * t2)) >> 1
                                     : (sqrt_fix(TA_ONE - 4 * u2) + TA_ONE) >> 1;

    case 22: return fmul(t2, fmul(EASE_BACK_S + TA_ONE, t) - EASE_BACK_S);
    case 23: return TA_ONE + fmul(u2, fmul(EASE_BACK_S + TA_ONE, -u) + EASE_BACK_S);
    case 24:
        s = fmul(EASE_BACK_S2 + TA_ONE, 2 * t) - EASE_BACK_S2;
        if (t < TA_ONE / 2)
            return fmul(4 * t2, s) >> 1;
        s = fmul(EASE_BACK_S2 + TA_ONE, 2 * u) - EASE_BACK_S2;
        return TA_ONE - (fmul(4 * u2, s) >> 1);

    case 25: return -fmul(exp2_fix(10 * t - 10 * TA_ONE), sin_turns((10 * t - 704512) / 3));
    case 26: return fmul(exp2_fix(-10 * t), sin_turns((10 * t - 49152) / 3)) + TA_ONE;
    case 27:
        if (t < TA_ONE / 2)
            return -(fmul(exp2_fix(20 * t - 10 * TA_ONE), sin_turns((20 * t - 729088) * 2 / 9)) >> 1);
        return (fmul(exp2_fix(-20 * t + 10 * TA_ONE), sin_turns((20 * t - 729088) * 2 / 9)) >> 1) + TA_ONE;

    case 28: return TA_ONE - bounce_out(u);
    case 29: return bounce_out(t);
    case 30: return (t < TA_ONE / 2) ? (TA_ONE - bounce_out(TA_ONE - 2 * t)) >> 1
                                     : (TA_ONE + bounce_out(2 * t - TA_ONE)) >> 1;

    default: return t;
    }
}

#define TA_EASE(kind, t) ta_ease((kind), (t))

#else /* TA_EASE supplied by the host */

int32_t ta_ease(int kind, int32_t t)
{
    return TA_EASE(kind, t);
}

#endif

#define TA_EASE_COUNT 31

static const char *const k_ease_names[TA_EASE_COUNT] = {
    "linear",
    "inquad",    "outquad",    "inoutquad",
    "incubic",   "outcubic",   "inoutcubic",
    "inquart",   "outquart",   "inoutquart",
    "inquint",   "outquint",   "inoutquint",
    "insine",    "outsine",    "inoutsine",
    "inexpo",    "outexpo",    "inoutexpo",
    "incirc",    "outcirc",    "inoutcirc",
    "inback",    "outback",    "inoutback",
    "inelastic", "outelastic", "inoutelastic",
    "inbounce",  "outbounce",  "inoutbounce",
};

int ta_ease_from_name(const char *name)
{
    int i;
    if (!name)
        return -1;
    for (i = 0; i < TA_EASE_COUNT; i++) {
        const char *candidate = k_ease_names[i];
        const char *given = name;
        while (*candidate && *given) {
            char c = *given;
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            if (c != *candidate)
                break;
            candidate++;
            given++;
        }
        if (!*candidate && !*given)
            return i;
    }
    return -1;
}

/* ==================================================================== JSON */

enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };

typedef struct jnode {
    uint8_t type;
    uint8_t negative;   /* number */
    uint8_t whole;      /* number written without a fraction or exponent */
    uint8_t truth;      /* bool */
    uint32_t magnitude; /* number: integer part */
    uint32_t fraction;  /* number: fractional part in 16.16 */
    const char *text;   /* string */
    const char *key;    /* when a member of an object */
    struct jnode *child;
    struct jnode *next;
    uint32_t count;     /* array or object: children */
} jnode;

typedef struct jparser {
    const char *at, *end;
    ta_arena *arena;
    const char *problem;
    int depth;
} jparser;

static const jnode k_null_node = { J_NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, 0 };

static void j_skip(jparser *p)
{
    for (;;) {
        while (p->at < p->end && (*p->at == ' ' || *p->at == '\t' || *p->at == '\n' || *p->at == '\r'))
            p->at++;
        /* Hand-edited maps have comments in them. */
        if (p->at + 1 < p->end && p->at[0] == '/' && p->at[1] == '/') {
            while (p->at < p->end && *p->at != '\n')
                p->at++;
            continue;
        }
        if (p->at + 1 < p->end && p->at[0] == '/' && p->at[1] == '*') {
            p->at += 2;
            while (p->at + 1 < p->end && !(p->at[0] == '*' && p->at[1] == '/'))
                p->at++;
            p->at = p->at + 2 <= p->end ? p->at + 2 : p->end;
            continue;
        }
        return;
    }
}

static int j_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static const char *j_string(jparser *p)
{
    const char *scan;
    char *out, *write;
    size_t length = 0;

    p->at++; /* the opening quote */
    for (scan = p->at; scan < p->end && *scan != '"'; scan++) {
        if (*scan == '\\' && scan + 1 < p->end)
            scan++;
        length++;
    }
    if (scan >= p->end) {
        p->problem = "a string is not closed";
        return NULL;
    }
    /* An escape never grows: \uXXXX is six bytes in, at most three out. */
    out = (char *)arena_alloc(p->arena, (size_t)(scan - p->at) + 1);
    if (!out) {
        p->problem = "out of memory";
        return NULL;
    }
    write = out;
    while (p->at < scan) {
        char c = *p->at++;
        if (c != '\\') {
            *write++ = c;
            continue;
        }
        c = *p->at++;
        switch (c) {
        case 'n': *write++ = '\n'; break;
        case 't': *write++ = '\t'; break;
        case 'r': *write++ = '\r'; break;
        case 'b': *write++ = '\b'; break;
        case 'f': *write++ = '\f'; break;
        case 'u': {
            uint32_t code = 0;
            int i;
            for (i = 0; i < 4 && p->at < scan; i++) {
                int digit = j_hex(*p->at);
                if (digit < 0)
                    break;
                code = code * 16 + (uint32_t)digit;
                p->at++;
            }
            if (code < 0x80) {
                *write++ = (char)code;
            } else if (code < 0x800) {
                *write++ = (char)(0xC0 | (code >> 6));
                *write++ = (char)(0x80 | (code & 0x3F));
            } else {
                *write++ = (char)(0xE0 | (code >> 12));
                *write++ = (char)(0x80 | ((code >> 6) & 0x3F));
                *write++ = (char)(0x80 | (code & 0x3F));
            }
            break;
        }
        default: *write++ = c; break;
        }
    }
    *write = 0;
    p->at = scan + 1;
    (void)length;
    return out;
}

static jnode *j_value(jparser *p);

static jnode *j_node(jparser *p, uint8_t type)
{
    jnode *node = (jnode *)arena_alloc(p->arena, sizeof(jnode));
    if (!node) {
        p->problem = "out of memory";
        return NULL;
    }
    node->type = type;
    return node;
}

static jnode *j_number(jparser *p)
{
    jnode *node = j_node(p, J_NUM);
    uint32_t magnitude = 0;
    int overflow = 0;
    if (!node)
        return NULL;
    node->whole = 1;
    if (p->at < p->end && (*p->at == '-' || *p->at == '+')) {
        node->negative = *p->at == '-';
        p->at++;
    }
    while (p->at < p->end && *p->at >= '0' && *p->at <= '9') {
        uint32_t digit = (uint32_t)(*p->at - '0');
        if (magnitude > (0xFFFFFFFFu - digit) / 10u)
            overflow = 1;
        else
            magnitude = magnitude * 10u + digit;
        p->at++;
    }
    node->magnitude = overflow ? 0xFFFFFFFFu : magnitude;
    if (p->at < p->end && *p->at == '.') {
        /* The fraction, to 16 bits: five decimal digits are more than enough. */
        uint32_t numerator = 0, scale = 1;
        int digits = 0;
        p->at++;
        while (p->at < p->end && *p->at >= '0' && *p->at <= '9') {
            if (digits < 5) {
                numerator = numerator * 10u + (uint32_t)(*p->at - '0');
                scale *= 10u;
                digits++;
            }
            p->at++;
        }
        {
            /* numerator / scale as sixteen binary places, a bit at a time:
             * no 64-bit divide anywhere in this file. */
            uint32_t rest = numerator, bits = 0;
            int bit;
            for (bit = 0; bit < 16; bit++) {
                rest <<= 1;
                bits <<= 1;
                if (rest >= scale) {
                    rest -= scale;
                    bits |= 1u;
                }
            }
            node->fraction = bits;
        }
        node->whole = 0;
    }
    if (p->at < p->end && (*p->at == 'e' || *p->at == 'E')) {
        /* Nobody writes a tile map in exponents; the mantissa is what we keep. */
        p->at++;
        if (p->at < p->end && (*p->at == '-' || *p->at == '+'))
            p->at++;
        while (p->at < p->end && *p->at >= '0' && *p->at <= '9')
            p->at++;
        node->whole = 0;
    }
    return node;
}

static int j_word(jparser *p, const char *word)
{
    size_t length = strlen(word);
    if ((size_t)(p->end - p->at) < length || memcmp(p->at, word, length) != 0)
        return 0;
    p->at += length;
    return 1;
}

static jnode *j_value(jparser *p)
{
    jnode *node, *last = NULL;
    char c;

    j_skip(p);
    if (p->at >= p->end) {
        p->problem = "the text ends early";
        return NULL;
    }
    if (++p->depth > 64) {
        p->problem = "nested too deeply";
        return NULL;
    }
    c = *p->at;

    if (c == '{' || c == '[') {
        const char close = c == '{' ? '}' : ']';
        node = j_node(p, c == '{' ? J_OBJ : J_ARR);
        if (!node)
            return NULL;
        p->at++;
        for (;;) {
            jnode *item;
            const char *key = NULL;
            j_skip(p);
            if (p->at >= p->end) {
                p->problem = "the text ends early";
                return NULL;
            }
            if (*p->at == close) {
                p->at++;
                break;
            }
            if (*p->at == ',') { /* also forgives a trailing comma */
                p->at++;
                continue;
            }
            if (c == '{') {
                if (*p->at != '"') {
                    p->problem = "a member has no name";
                    return NULL;
                }
                key = j_string(p);
                if (!key)
                    return NULL;
                j_skip(p);
                if (p->at >= p->end || *p->at != ':') {
                    p->problem = "a member has no value";
                    return NULL;
                }
                p->at++;
            }
            item = j_value(p);
            if (!item)
                return NULL;
            item->key = key;
            if (last)
                last->next = item;
            else
                node->child = item;
            last = item;
            node->count++;
        }
    } else if (c == '"') {
        node = j_node(p, J_STR);
        if (!node)
            return NULL;
        node->text = j_string(p);
        if (!node->text)
            return NULL;
    } else if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
        node = j_number(p);
    } else if (j_word(p, "true")) {
        node = j_node(p, J_BOOL);
        if (node)
            node->truth = 1;
    } else if (j_word(p, "false")) {
        node = j_node(p, J_BOOL);
    } else if (j_word(p, "null")) {
        node = j_node(p, J_NULL);
    } else {
        p->problem = "something that is not JSON";
        return NULL;
    }
    p->depth--;
    return node;
}

static const jnode *j_parse(ta_arena *arena, const char *text, size_t length, char *error, size_t error_size)
{
    jparser parser;
    jnode *root;
    parser.at = text;
    parser.end = text + length;
    parser.arena = arena;
    parser.problem = NULL;
    parser.depth = 0;
    /* A byte order mark is not JSON, but files have them. */
    if (length >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
        parser.at += 3;
    root = j_value(&parser);
    if (!root)
        set_error(error, error_size, "This is not readable JSON: ", parser.problem ? parser.problem : "unknown");
    return root;
}

/* --- lenient accessors, as the editor's: the wrong type answers the fallback --- */

static const jnode *j_get(const jnode *object, const char *key)
{
    const jnode *child;
    if (!object || object->type != J_OBJ)
        return &k_null_node;
    for (child = object->child; child; child = child->next)
        if (child->key && strcmp(child->key, key) == 0)
            return child;
    return &k_null_node;
}

static int j_has(const jnode *object, const char *key)
{
    return j_get(object, key) != &k_null_node;
}

/* The leading whole number of a string; 0 when there is none. */
static int text_to_int(const char *text, int32_t *out)
{
    int negative = 0, any = 0;
    uint32_t magnitude = 0;
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
        text++;
    if (*text == '-' || *text == '+') {
        negative = *text == '-';
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        if (magnitude < 214748364u)
            magnitude = magnitude * 10u + (uint32_t)(*text - '0');
        else
            magnitude = 0x7FFFFFFFu;
        any = 1;
        text++;
    }
    if (!any)
        return 0;
    if (magnitude > 0x7FFFFFFFu)
        magnitude = 0x7FFFFFFFu;
    *out = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    return 1;
}

static int32_t j_int(const jnode *node, int32_t fallback)
{
    int32_t parsed;
    switch (node->type) {
    case J_NUM: {
        uint32_t magnitude = node->magnitude > 0x7FFFFFFFu ? 0x7FFFFFFFu : node->magnitude;
        return node->negative ? -(int32_t)magnitude : (int32_t)magnitude;
    }
    case J_BOOL: return node->truth ? 1 : 0;
    case J_STR: return text_to_int(node->text, &parsed) ? parsed : fallback;
    default: return fallback;
    }
}

/* A tile id uses all 32 bits. */
static uint32_t j_gid(const jnode *node)
{
    return node->type == J_NUM && !node->negative ? node->magnitude : 0;
}

static int j_bool(const jnode *node, int fallback)
{
    if (node->type == J_BOOL)
        return node->truth;
    if (node->type == J_NUM && node->whole)
        return node->magnitude != 0;
    return fallback;
}

/* A number as 16.16, for the two places a map has fractions (opacity, offsets). */
static int32_t j_fixed(const jnode *node, int32_t fallback)
{
    if (node->type == J_NUM) {
        uint32_t magnitude = node->magnitude > 32767u ? 32767u : node->magnitude;
        int32_t value = (int32_t)((magnitude << 16) | (node->fraction & 0xFFFFu));
        return node->negative ? -value : value;
    }
    if (node->type == J_STR) {
        int32_t parsed;
        if (text_to_int(node->text, &parsed))
            return clamp_int(parsed, -32767, 32767) * TA_ONE;
    }
    return fallback;
}

static const char *int_to_text(ta_arena *arena, const jnode *node)
{
    char buffer[16];
    int at = 15;
    uint32_t magnitude = node->magnitude;
    buffer[at] = 0;
    do {
        buffer[--at] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude && at > 1);
    if (node->negative)
        buffer[--at] = '-';
    return arena_string(arena, buffer + at, (size_t)(15 - at));
}

static const char *j_text(ta_arena *arena, const jnode *node, const char *fallback)
{
    switch (node->type) {
    /* Copied: the parsed text is thrown away once the map is built. */
    case J_STR: return arena_string(arena, node->text, strlen(node->text));
    case J_NUM: return int_to_text(arena, node);
    case J_BOOL: return node->truth ? "true" : "false";
    default: return fallback;
    }
}

/* ================================================================= the map */

typedef struct ta_internal {
    ta_arena arena;
    ta_options options;
    int *anim_base;      /* per layer: its first animation among all the map's */
    int *track_base;     /* per animation: its first track among all the map's */
    int anim_total, track_total;
    int32_t *swap_offset; /* per layer: where its swapped cells start; -1 = it has no swap track */
    size_t swap_bytes;
    /* Mutable views of what ta_map exposes as const. */
    ta_layer *layers;
    ta_tileset *tilesets;
    ta_event *events;
    int event_capacity;
} ta_internal;

static int parse_color(const char *text, ta_color *out)
{
    size_t start = text[0] == '#' ? 1 : 0;
    size_t digits = strlen(text) - start, first, i;
    uint8_t bytes[4] = { 255, 0, 0, 0 };
    if (digits != 6 && digits != 8)
        return 0;
    first = digits == 8 ? 0 : 1;
    for (i = 0; i < digits / 2; i++) {
        int hi = j_hex(text[start + i * 2]);
        int lo = j_hex(text[start + i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        bytes[first + i] = (uint8_t)(hi * 16 + lo);
    }
    out->r = bytes[1];
    out->g = bytes[2];
    out->b = bytes[3];
    out->a = bytes[0];
    return 1;
}

/* Events are known by index. "always" and "never" are not events. */
static int intern_event(ta_map *map, const char *name)
{
    ta_internal *in = (ta_internal *)map->internal;
    int i;
    if (!name || !*name)
        return TA_TRIGGER_NEVER;
    if (strcmp(name, "always") == 0)
        return TA_TRIGGER_ALWAYS;
    if (strcmp(name, "never") == 0)
        return TA_TRIGGER_NEVER;
    for (i = 0; i < map->event_count; i++)
        if (strcmp(in->events[i].name, name) == 0)
            return i;
    if (map->event_count >= in->event_capacity)
        return TA_TRIGGER_NEVER;
    in->events[map->event_count].name = name;
    return map->event_count++;
}

static int layer_by_name(const ta_map *map, const char *name)
{
    int i;
    for (i = 0; i < map->layer_count; i++)
        if (strcmp(map->layers[i].name, name) == 0)
            return i;
    return -1;
}

static void read_properties(ta_arena *arena, const jnode *list, int *count, const ta_property **out)
{
    ta_property *properties;
    const jnode *item;
    int n = 0;
    *count = 0;
    *out = NULL;
    if (list->type != J_ARR || list->count == 0)
        return;
    properties = (ta_property *)arena_alloc(arena, sizeof(ta_property) * list->count);
    if (!properties)
        return;
    for (item = list->child; item; item = item->next) {
        const jnode *value = j_get(item, "value");
        properties[n].name = j_text(arena, j_get(item, "name"), "");
        properties[n].type = j_text(arena, j_get(item, "type"), "string");
        properties[n].text = j_text(arena, value, "");
        properties[n].number = j_int(value, 0);
        n++;
    }
    *count = n;
    *out = properties;
}

/* 16.16 to a whole number, rounded down whatever the sign. */
static int floor_fixed(int32_t value)
{
    const uint32_t magnitude = value >= 0 ? (uint32_t)value : 0u - (uint32_t)value;
    return value >= 0 ? (int)(magnitude >> 16) : -(int)((magnitude + 0xFFFFu) >> 16);
}

static int read_objects(ta_arena *arena, const jnode *list, ta_layer *layer)
{
    ta_object *objects;
    const jnode *item;
    int n = 0;
    if (list->type != J_ARR || list->count == 0)
        return 1;
    objects = (ta_object *)arena_alloc(arena, sizeof(ta_object) * list->count);
    if (!objects)
        return 0;
    for (item = list->child; item; item = item->next) {
        ta_object *object = &objects[n++];
        object->id = j_int(j_get(item, "id"), 0);
        object->name = j_text(arena, j_get(item, "name"), "");
        object->class_name = j_has(item, "type") ? j_text(arena, j_get(item, "type"), "")
                                                 : j_text(arena, j_get(item, "class"), "");
        object->x = floor_fixed(j_fixed(j_get(item, "x"), 0));
        object->y = floor_fixed(j_fixed(j_get(item, "y"), 0));
        object->width = floor_fixed(j_fixed(j_get(item, "width"), 0));
        object->height = floor_fixed(j_fixed(j_get(item, "height"), 0));
        object->point = j_bool(j_get(item, "point"), 0);
        object->visible = j_bool(j_get(item, "visible"), 1);
        read_properties(arena, j_get(item, "properties"), &object->property_count, &object->properties);
    }
    layer->object_count = n;
    layer->objects = objects;
    return 1;
}

static void read_tileset_fields(ta_map *map, const jnode *json, ta_tileset *tileset)
{
    ta_internal *in = (ta_internal *)map->internal;
    ta_arena *arena = &in->arena;
    const char *key;

    tileset->name = j_text(arena, j_get(json, "name"), "");
    tileset->image = j_text(arena, j_get(json, "image"), "");
    tileset->image_width = j_int(j_get(json, "imagewidth"), 0);
    tileset->image_height = j_int(j_get(json, "imageheight"), 0);
    tileset->tile_width = j_int(j_get(json, "tilewidth"), 16);
    tileset->tile_height = j_int(j_get(json, "tileheight"), 16);
    if (tileset->tile_width < 1) tileset->tile_width = 1;
    if (tileset->tile_height < 1) tileset->tile_height = 1;
    tileset->margin = j_int(j_get(json, "margin"), 0);
    tileset->spacing = j_int(j_get(json, "spacing"), 0);
    tileset->columns = j_int(j_get(json, "columns"), 0);
    tileset->tile_count = j_int(j_get(json, "tilecount"), 0);
    key = j_text(arena, j_get(json, "transparentcolor"), "");
    tileset->has_transparent = *key && parse_color(key, &tileset->transparent);

}

static void read_palette(ta_map *map, const jnode *json)
{
    ta_internal *in = (ta_internal *)map->internal;
    ta_arena *arena = &in->arena;
    const jnode *list = j_get(json, "colors"), *item;
    ta_color *colors;
    int n = 0, transparent;

    map->transparent = -1;
    if (list->type != J_ARR || list->count == 0)
        return;
    colors = (ta_color *)arena_alloc(arena, sizeof(ta_color) * 256);
    if (!colors)
        return;
    for (item = list->child; item && n < 256; item = item->next) {
        ta_color color;
        if (item->type != J_STR || !parse_color(item->text, &color))
            continue;
        color.a = 255;
        colors[n++] = color;
    }
    if (n == 0)
        return;
    transparent = j_int(j_get(json, "transparent"), -1);
    if (transparent >= 0 && transparent < n) {
        map->transparent = transparent;
        colors[transparent].a = 0;
    }
    map->palette_count = n;
    map->palette = colors;

    list = j_get(json, "cycles");
    if (list->type == J_ARR && list->count) {
        ta_cycle *cycles = (ta_cycle *)arena_alloc(arena, sizeof(ta_cycle) * list->count);
        int c = 0;
        if (!cycles)
            return;
        for (item = list->child; item; item = item->next) {
            ta_cycle *cycle = &cycles[c++];
            cycle->name = j_text(arena, j_get(item, "name"), "");
            cycle->first = clamp_int(j_int(j_get(item, "first"), 0), 0, 255);
            cycle->last = clamp_int(j_int(j_get(item, "last"), 0), 0, 255);
            if (cycle->last < cycle->first) {
                int swap = cycle->first;
                cycle->first = cycle->last;
                cycle->last = swap;
            }
            cycle->frames = clamp_int(j_int(j_get(item, "frames"), 4), 1, 32767);
            cycle->reverse = j_bool(j_get(item, "reverse"), 0);
            cycle->trigger = intern_event(map, j_text(arena, j_get(item, "trigger"), "always"));
        }
        map->cycle_count = c;
        map->cycles = cycles;
    }

    list = j_get(json, "variants");
    if (list->type == J_ARR && list->count) {
        ta_variant *variants = (ta_variant *)arena_alloc(arena, sizeof(ta_variant) * list->count);
        int v = 0;
        if (!variants)
            return;
        for (item = list->child; item; item = item->next) {
            const jnode *swatches = j_get(item, "swatches"), *swatch, *color;
            const jnode *single = j_get(item, "colors");
            const char *name = j_text(arena, j_get(item, "name"), "");
            uint8_t *entries;
            ta_color *values;
            int count = 0, room = 0;
            if (!*name)
                continue;
            /* Swatches: one color or a run of them, copied in at an entry. */
            if (swatches->type == J_ARR)
                for (swatch = swatches->child; swatch; swatch = swatch->next)
                    room += (int)j_get(swatch, "colors")->count;
            room += (int)single->count;
            entries = (uint8_t *)arena_alloc(arena, (size_t)(room ? room : 1));
            values = (ta_color *)arena_alloc(arena, sizeof(ta_color) * (size_t)(room ? room : 1));
            if (!entries || !values)
                return;
            if (swatches->type == J_ARR) {
                for (swatch = swatches->child; swatch; swatch = swatch->next) {
                    const jnode *run = j_get(swatch, "colors");
                    int at = clamp_int(j_int(j_get(swatch, "at"), 0), 0, 255);
                    if (run->type != J_ARR)
                        continue;
                    for (color = run->child; color && at < 256; color = color->next) {
                        ta_color rgba;
                        if (color->type != J_STR || !parse_color(color->text, &rgba))
                            continue;
                        rgba.a = 255;
                        entries[count] = (uint8_t)at++;
                        values[count] = rgba;
                        count++;
                    }
                }
            }
            /* An earlier form: "colors": { "<entry>": "#rrggbb" }. */
            if (single->type == J_OBJ) {
                for (color = single->child; color; color = color->next) {
                    int32_t entry;
                    ta_color rgba;
                    if (!text_to_int(color->key, &entry) || entry < 0 || entry > 255)
                        continue;
                    if (color->type != J_STR || !parse_color(color->text, &rgba))
                        continue;
                    rgba.a = 255;
                    entries[count] = (uint8_t)entry;
                    values[count] = rgba;
                    count++;
                }
            }
            variants[v].name = name;
            variants[v].count = count;
            variants[v].entries = entries;
            variants[v].colors = values;
            v++;
        }
        map->variant_count = v;
        map->variants = variants;
    }
}

static const uint8_t *decode_base64(ta_arena *arena, const char *text, size_t *size)
{
    size_t length = strlen(text), at = 0;
    uint8_t *out = (uint8_t *)arena_alloc(arena, length / 4 * 3 + 4);
    uint32_t buffer = 0;
    int bits = 0;
    if (!out)
        return NULL;
    for (; *text; text++) {
        char c = *text;
        int value;
        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '+') value = 62;
        else if (c == '/') value = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        else return NULL;
        buffer = (buffer << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[at++] = (uint8_t)((buffer >> bits) & 0xFF);
        }
    }
    *size = at;
    return out;
}

/* A layer's tiles, whatever way Tiled wrote them, as `cells` ids. */
static uint32_t *decode_layer_data(ta_map *map, const jnode *layer, size_t cells, const char **problem)
{
    ta_internal *in = (ta_internal *)map->internal;
    ta_arena *arena = &in->arena;
    const jnode *source = j_get(layer, "data");
    uint32_t *data = (uint32_t *)arena_alloc(arena, sizeof(uint32_t) * (cells ? cells : 1));
    size_t i = 0;

    if (!data) {
        *problem = "out of memory";
        return NULL;
    }
    if (j_has(layer, "chunks")) {
        *problem = "a layer is stored in chunks (an infinite map), which is not supported";
        return NULL;
    }
    if (source->type == J_ARR) {
        const jnode *cell;
        for (cell = source->child; cell && i < cells; cell = cell->next)
            data[i++] = j_gid(cell);
    } else if (source->type == J_STR) {
        const char *encoding = j_text(arena, j_get(layer, "encoding"), "csv");
        const char *compression = j_text(arena, j_get(layer, "compression"), "");
        const uint8_t *bytes;
        size_t size = 0;
        if (strcmp(encoding, "base64") != 0) {
            *problem = "a layer uses an unknown encoding";
            return NULL;
        }
        bytes = decode_base64(arena, source->text, &size);
        if (!bytes) {
            *problem = "a layer has damaged base64 data";
            return NULL;
        }
        if (*compression) {
            uint8_t *inflated;
            if ((strcmp(compression, "zlib") != 0 && strcmp(compression, "gzip") != 0) || !in->options.inflate) {
                *problem = "a layer's data is compressed; save the map with CSV layer data";
                return NULL;
            }
            inflated = (uint8_t *)arena_alloc(arena, cells * 4 + 4);
            if (!inflated || !in->options.inflate(in->options.inflate_user, bytes, size, inflated, cells * 4)) {
                *problem = "a layer has damaged compressed data";
                return NULL;
            }
            bytes = inflated;
            size = cells * 4;
        }
        for (i = 0; i < cells && i * 4 + 3 < size; i++) {
            const uint8_t *p = bytes + i * 4;
            data[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
    }
    /* Too few are padded with nothing; too many were never read. */
    return data;
}

static void read_key(const jnode *json, ta_key *key, ta_arena *arena)
{
    int ease;
    key->f = clamp_int(j_int(j_get(json, "f"), 0), 0, 32767);
    key->x = clamp_int(j_int(j_get(json, "x"), 0), -32768, 32767);
    key->y = clamp_int(j_int(j_get(json, "y"), 0), -32768, 32767);
    key->alpha = clamp_int(j_int(j_get(json, "alpha"), 255), 0, 255);
    key->cel = clamp_int(j_int(j_get(json, "cel"), 0), -32768, 32767);
    key->rate = clamp_int(j_int(j_get(json, "rate"), 1), 0, 4096);
    key->period = clamp_int(j_int(j_get(json, "period"), 32), 1, 32767);
    key->on = clamp_int(j_int(j_get(json, "on"), 16), 0, 32767);
    ease = ta_ease_from_name(j_text(arena, j_get(json, "ease"), "linear"));
    key->ease = (uint8_t)(ease < 0 ? 0 : ease);
    key->tween = (uint8_t)j_bool(j_get(json, "tween"), 1);
}

static void default_key(ta_key *key)
{
    memset(key, 0, sizeof *key);
    key->alpha = 255;
    key->rate = 1;
    key->period = 32;
    key->on = 16;
    key->tween = 1;
}

/* Sorted by frame; of two keys on one frame the later in the file wins. */
static int sort_keys(ta_key *keys, int count)
{
    int i, j, kept = 0;
    for (i = 1; i < count; i++) { /* insertion sort: stable, and tracks are short */
        ta_key key = keys[i];
        for (j = i; j > 0 && keys[j - 1].f > key.f; j--)
            keys[j] = keys[j - 1];
        keys[j] = key;
    }
    for (i = 0; i < count; i++) {
        if (i + 1 < count && keys[i].f == keys[i + 1].f)
            continue;
        keys[kept++] = keys[i];
    }
    return kept;
}

/* Answers 0 for a track of a kind this version does not know. `source_name`
 * is resolved to a layer once every layer has been read. */
static int read_track(ta_map *map, const jnode *json, int duration, ta_track *track, const char **source_name)
{
    ta_internal *in = (ta_internal *)map->internal;
    ta_arena *arena = &in->arena;
    const char *kind = j_text(arena, j_get(json, "kind"), "blit");
    const jnode *keys = j_get(json, "keys");
    const int legacy = !j_has(json, "keys");
    ta_key *list;
    int count = 0, room;

    track->name = j_text(arena, j_get(json, "name"), "");
    track->muted = j_bool(j_get(json, "muted"), 0);
    track->src_w = 1;
    track->src_h = 1;
    track->frames = 1;
    *source_name = "";

    room = (keys->type == J_ARR ? (int)keys->count : 0) + 2;
    list = (ta_key *)arena_alloc(arena, sizeof(ta_key) * (size_t)room);
    if (!list)
        return 0;

    if (strcmp(kind, "blit") == 0) {
        const jnode *src = j_get(json, "src"), *dst = j_get(json, "dst");
        const jnode *mask = j_get(json, "mask"), *anchor = j_get(json, "anchor");
        track->kind = TA_TRACK_BLIT;
        *source_name = j_text(arena, j_get(json, "from"), "");
        track->src_x = j_int(j_get(src, "x"), 0);
        track->src_y = j_int(j_get(src, "y"), 0);
        track->src_w = j_int(j_get(src, "w"), 1);
        track->src_h = j_int(j_get(src, "h"), 1);
        track->frames = j_int(j_get(src, "frames"), 1);
        track->stride = j_int(j_get(src, "stride"), 0);
        if (track->src_w < 1) track->src_w = 1;
        if (track->src_h < 1) track->src_h = 1;
        if (track->frames < 1) track->frames = 1;
        if (track->stride < 0) track->stride = 0;
        if (track->stride == 0) track->stride = track->src_w;
        track->cycle = j_bool(j_get(json, "cycle"), 0);
        track->dst_x = j_int(j_get(dst, "x"), 0);
        track->dst_y = j_int(j_get(dst, "y"), 0);
        track->mask_x = j_int(j_get(mask, "x"), 0);
        track->mask_y = j_int(j_get(mask, "y"), 0);
        track->mask_w = j_int(j_get(mask, "w"), 0);
        track->mask_h = j_int(j_get(mask, "h"), 0);
        if (track->mask_w < 0) track->mask_w = 0;
        if (track->mask_h < 0) track->mask_h = 0;
        if (!(track->mask_w > 0 && track->mask_h > 0))
            track->mask_x = track->mask_y = track->mask_w = track->mask_h = 0;
        track->anchor_x = j_int(j_get(anchor, "x"), 0);
        track->anchor_y = j_int(j_get(anchor, "y"), 0);
        if (legacy) {
            default_key(&list[count++]);
            if (track->frames > 1) {
                default_key(&list[count]);
                list[count].f = duration;
                list[count].cel = track->frames;
                count++;
            }
        }
    } else if (strcmp(kind, "swap") == 0) {
        const jnode *region = j_get(json, "region");
        track->kind = TA_TRACK_SWAP;
        *source_name = j_text(arena, j_get(json, "with"), "");
        track->src_x = j_int(j_get(region, "x"), 0);
        track->src_y = j_int(j_get(region, "y"), 0);
        track->src_w = j_int(j_get(region, "w"), 0);
        track->src_h = j_int(j_get(region, "h"), 0);
        if (track->src_x < 0) track->src_x = 0;
        if (track->src_y < 0) track->src_y = 0;
        if (track->src_w < 0) track->src_w = 0;
        if (track->src_h < 0) track->src_h = 0;
        track->seed = (uint32_t)j_int(j_get(json, "seed"), 0);
        if (legacy)
            default_key(&list[count++]);
    } else if (strcmp(kind, "layer") == 0 || strcmp(kind, "shift") == 0 || strcmp(kind, "fade") == 0) {
        const jnode *drift = j_get(json, "drift");
        track->kind = TA_TRACK_LAYER;
        track->wrap = j_bool(j_get(json, "wrap"), 0);
        track->drift_speed = clamp_int(j_int(j_get(drift, "speed"), 0), 0, 4096);
        track->drift_change = clamp_int(j_int(j_get(drift, "change"), 240), 1, 32767);
        track->seed = (uint32_t)j_int(j_get(drift, "seed"), 0);
        if (legacy) {
            /* The first draft's keyless tracks: one tween over the whole pass. */
            default_key(&list[0]);
            default_key(&list[1]);
            list[1].f = duration;
            if (strcmp(kind, "shift") == 0) {
                list[1].x = j_int(j_get(json, "dxPx"), 0);
                list[1].y = j_int(j_get(json, "dyPx"), 0);
            } else if (strcmp(kind, "fade") == 0) {
                list[0].alpha = clamp_int(j_int(j_get(json, "from"), 255), 0, 255);
                list[1].alpha = clamp_int(j_int(j_get(json, "to"), 255), 0, 255);
            }
            count = 2;
        }
    } else {
        return 0;
    }

    if (keys->type == J_ARR) {
        const jnode *key;
        for (key = keys->child; key; key = key->next)
            read_key(key, &list[count++], arena);
    }
    track->key_count = sort_keys(list, count);
    track->keys = list;
    return 1;
}

typedef struct pending_source {
    ta_track *track;
    int layer;
    const char *name;
    struct pending_source *next;
} pending_source;

static void read_animations(ta_map *map, int layer_index, const jnode *root, ta_layer *layer, pending_source **pending)
{
    ta_internal *in = (ta_internal *)map->internal;
    ta_arena *arena = &in->arena;
    const jnode *first = NULL, *item;
    ta_animation *animations;
    int count = 0, n = 0, single = 0;

    /* An array, {"animations": [...]}, or one animation by itself. */
    if (root->type == J_ARR) {
        first = root->child;
        count = (int)root->count;
    } else if (root->type == J_OBJ && j_has(root, "animations")) {
        const jnode *list = j_get(root, "animations");
        if (list->type == J_ARR) {
            first = list->child;
            count = (int)list->count;
        }
    } else if (root->type == J_OBJ) {
        first = root;
        count = 1;
        single = 1;
    }
    if (count == 0)
        return;
    animations = (ta_animation *)arena_alloc(arena, sizeof(ta_animation) * (size_t)count);
    if (!animations)
        return;

    for (item = first; item; item = single ? NULL : item->next) {
        ta_animation *anim;
        const jnode *tracks, *track_json;
        ta_track *list;
        int ease, t = 0;
        if (item->type != J_OBJ)
            continue;
        anim = &animations[n];
        anim->id = j_text(arena, j_get(item, "id"), "");
        anim->trigger = intern_event(map, j_text(arena, j_get(item, "trigger"), "always"));
        anim->delay = j_int(j_get(item, "delay"), 0);
        if (anim->delay < 0) anim->delay = 0;
        anim->duration = clamp_int(j_int(j_get(item, "duration"), 24), 1, 32767);
        anim->loop = j_bool(j_get(item, "loop"), 1);
        anim->pingpong = j_bool(j_get(item, "pingpong"), 0);
        ease = ta_ease_from_name(j_text(arena, j_get(item, "ease"), "linear"));
        anim->ease = ease < 0 ? 0 : ease;
        {
            const char *then = j_text(arena, j_get(item, "then"), "");
            int event = *then ? intern_event(map, then) : -1;
            /* "always" and "never" fire nothing. */
            anim->then = event >= 0 ? event : -1;
        }
        tracks = j_get(item, "tracks");
        list = (ta_track *)arena_alloc(arena, sizeof(ta_track) * (tracks->type == J_ARR && tracks->count ? tracks->count : 1));
        if (!list)
            return;
        if (tracks->type == J_ARR) {
            for (track_json = tracks->child; track_json; track_json = track_json->next) {
                const char *source_name = "";
                pending_source *wait;
                if (!read_track(map, track_json, anim->duration, &list[t], &source_name))
                    continue;
                list[t].source = layer_index;
                if (*source_name) {
                    wait = (pending_source *)arena_alloc(arena, sizeof(pending_source));
                    if (!wait)
                        return;
                    wait->track = &list[t];
                    wait->layer = layer_index;
                    wait->name = source_name;
                    wait->next = *pending;
                    *pending = wait;
                }
                t++;
            }
        }
        anim->track_count = t;
        anim->tracks = list;
        if (!*anim->id) {
            /* Ids are how an action names an animation. */
            char buffer[20] = "anim";
            int number = n + 1, at = 4, digits = 0, d;
            char reversed[12];
            do {
                reversed[digits++] = (char)('0' + number % 10);
                number /= 10;
            } while (number);
            for (d = digits - 1; d >= 0; d--)
                buffer[at++] = reversed[d];
            anim->id = arena_string(arena, buffer, (size_t)at);
        }
        n++;
    }
    layer->animation_count = n;
    layer->animations = animations;
}

static void read_action(ta_map *map, const jnode *json, ta_action *action, const char **layer_name,
                        const char **group_name)
{
    static const char *const kinds[] = { "play", "stop", "pause", "rewind", "set", "show", "hide",
                                         "toggle", "restore", "palette", "fire" };
    static const char *const attributes[] = { "delay", "duration", "loop", "pingpong", "ease" };
    ta_internal *in = (ta_internal *)map->internal;
    ta_arena *arena = &in->arena;
    const char *kind = j_text(arena, j_get(json, "do"), "play");
    const char *attribute = j_text(arena, j_get(json, "attribute"), "");
    const char *value = j_text(arena, j_get(json, "value"), "");
    int i;

    action->kind = TA_DO_NOTHING;
    for (i = 0; i < (int)(sizeof kinds / sizeof kinds[0]); i++)
        if (strcmp(kind, kinds[i]) == 0)
            action->kind = i;
    action->layer = -1;
    action->palette = -1;
    action->event = -1;
    action->animation = j_text(arena, j_get(json, "animation"), "");
    *layer_name = j_text(arena, j_get(json, "layer"), "");
    *group_name = j_text(arena, j_get(json, "group"), "");
    action->group = -1;

    action->attribute = TA_SET_NOTHING;
    for (i = 0; i < (int)(sizeof attributes / sizeof attributes[0]); i++)
        if (strcmp(attribute, attributes[i]) == 0)
            action->attribute = i;
    action->has_number = text_to_int(value, &action->number);
    action->flag = strcmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcmp(value, "yes") == 0
                || strcmp(value, "on") == 0;
    action->ease = ta_ease_from_name(value);

    if (action->kind == TA_DO_FIRE) {
        const char *event = *value ? value : j_text(arena, j_get(json, "event"), "");
        int index = intern_event(map, event);
        action->event = index >= 0 ? index : -1;
    } else if (action->kind == TA_DO_PALETTE) {
        /* Kept as text until the palette is read. */
        action->animation = j_text(arena, j_get(json, "palette"), "");
    }
}

/* The one tileset a layer's own tiles come from. */
static int own_tileset(const ta_map *map, const ta_layer *layer)
{
    int found = TA_TILESET_NONE, index, k;
    uint32_t low = 1, high = 0;
    size_t cell, cells;
    if (!layer->data)
        return found;
    cells = (size_t)layer->width * (size_t)layer->height;
    for (cell = 0; cell < cells; cell++) {
        const uint32_t gid = layer->data[cell] & TA_GID_MASK;
        /* Most cells are settled by the last answer's range of ids. */
        if (gid == 0 || (gid >= low && gid < high))
            continue;
        index = ta_map_tileset_of(map, gid);
        if (index < 0)
            continue;
        if (found != TA_TILESET_NONE && found != index)
            return TA_TILESET_MIXED;
        found = index;
        low = map->tilesets[index].first_gid;
        high = 0xFFFFFFFFu;
        for (k = 0; k < map->tileset_count; k++)
            if (map->tilesets[k].first_gid > low && map->tilesets[k].first_gid < high)
                high = map->tilesets[k].first_gid;
    }
    return found;
}

ta_map *ta_map_load(const char *json, size_t length, const ta_options *options, char *error, size_t error_size)
{
    ta_arena scratch, arena;
    ta_allocator allocator;
    const jnode *root, *extension, *list, *item;
    ta_map *map;
    ta_internal *in;
    pending_source *pending = NULL;
    const char **action_layers = NULL, **action_groups = NULL;
    uint8_t *defined = NULL;
    int i, a, capacity, action_total = 0, action_at = 0;

    if (!ta_allocator_from(options ? options->allocator : NULL, &allocator)) {
        set_error(error, error_size, "No allocator was given.", NULL);
        return NULL;
    }
    memset(&scratch, 0, sizeof scratch);
    memset(&arena, 0, sizeof arena);
    scratch.allocator = allocator;
    arena.allocator = allocator;

    root = j_parse(&scratch, json, length, error, error_size);
    if (!root) {
        arena_free(&scratch);
        return NULL;
    }
    if (root->type != J_OBJ || !j_has(root, "layers") || !j_has(root, "width")) {
        set_error(error, error_size, "This is not a Tiled map (no \"layers\" or \"width\").", NULL);
        arena_free(&scratch);
        return NULL;
    }

    map = (ta_map *)arena_alloc(&arena, sizeof(ta_map));
    in = (ta_internal *)arena_alloc(&arena, sizeof(ta_internal));
    if (!map || !in)
        goto no_memory;
    map->internal = in;
    if (options)
        in->options = *options;
    in->options.allocator = NULL;

    map->width = j_int(j_get(root, "width"), 0);
    map->height = j_int(j_get(root, "height"), 0);
    map->tile_width = j_int(j_get(root, "tilewidth"), 0);
    map->tile_height = j_int(j_get(root, "tileheight"), 0);
    if (map->width <= 0 || map->height <= 0 || map->tile_width <= 0 || map->tile_height <= 0) {
        set_error(error, error_size, "The map has no size.", NULL);
        goto fail;
    }
    if (map->width > 32767 || map->height > 32767 || map->width * map->height > 16 * 1024 * 1024) {
        set_error(error, error_size, "The map is too large.", NULL);
        goto fail;
    }
    if (j_bool(j_get(root, "infinite"), 0)) {
        set_error(error, error_size, "Infinite maps are not supported.", NULL);
        goto fail;
    }

    extension = j_get(root, "tileagent");
    map->version = j_int(j_get(extension, "version"), 0);
    map->fps = clamp_int(j_int(j_get(extension, "fps"), 0), 0, 1000);
    if (map->fps == 0)
        map->fps = 60;

    /* Room for every event anything could name. */
    capacity = 1;
    list = j_get(extension, "events");
    if (list->type == J_ARR) {
        capacity += (int)list->count;
        for (item = list->child; item; item = item->next) {
            const jnode *actions = j_get(item, "actions");
            if (actions->type == J_ARR) {
                capacity += (int)actions->count;
                action_total += (int)actions->count;
            }
        }
    }
    list = j_get(root, "layers");
    for (item = list->type == J_ARR ? list->child : NULL; item; item = item->next) {
        const jnode *animations = j_get(item, "animations");
        if (animations->type == J_OBJ && j_has(animations, "animations"))
            animations = j_get(animations, "animations");
        capacity += animations->type == J_ARR ? 2 * (int)animations->count : 2;
    }
    capacity += (int)j_get(j_get(extension, "palette"), "cycles")->count;

    in->arena = arena; /* intern_event allocates nothing, but keep the two in step */
    in->event_capacity = capacity;
    in->events = (ta_event *)arena_alloc(&arena, sizeof(ta_event) * (size_t)capacity);
    action_layers = (const char **)arena_alloc(&scratch, sizeof(char *) * (size_t)(action_total + 1));
    action_groups = (const char **)arena_alloc(&scratch, sizeof(char *) * (size_t)(action_total + 1));
    defined = (uint8_t *)arena_alloc(&scratch, (size_t)capacity);
    if (!in->events || !action_layers || !action_groups || !defined)
        goto no_memory;
    map->events = in->events;
    in->arena = arena;

    /* --- events: an object, or a bare string for one with no actions --- */
    list = j_get(extension, "events");
    for (item = list->type == J_ARR ? list->child : NULL; item; item = item->next) {
        const char *name = item->type == J_OBJ ? j_text(&in->arena, j_get(item, "name"), "")
                                               : j_text(&in->arena, item, "");
        int index;
        if (!*name)
            continue;
        /* An earlier event's "fire" may have named this one already; that is
         * not a definition. */
        index = intern_event(map, name);
        if (index < 0 || defined[index])
            continue; /* "always", "never", or a name defined twice: the first stands */
        defined[index] = 1;
        if (item->type == J_OBJ) {
            const jnode *actions = j_get(item, "actions"), *action_json;
            ta_action *out;
            int n = 0;
            if (actions->type != J_ARR || actions->count == 0)
                continue;
            out = (ta_action *)arena_alloc(&in->arena, sizeof(ta_action) * actions->count);
            if (!out)
                goto no_memory_in;
            /* The event table may be extended by a "fire" naming a new event;
             * it never moves, so `index` stays good. */
            for (action_json = actions->child; action_json; action_json = action_json->next) {
                /* Layer and tileset names wait until both are read. `layer`
                 * holds the action's number in the meantime. */
                read_action(map, action_json, &out[n], &action_layers[action_at], &action_groups[action_at]);
                out[n].layer = action_at++;
                n++;
            }
            in->events[index].action_count = n;
            in->events[index].actions = out;
        }
    }

    /* --- the one palette, if the map is indexed --- */
    map->transparent = -1;
    read_palette(map, j_get(extension, "palette"));

    /* --- tilesets --- */
    list = j_get(root, "tilesets");
    map->tileset_count = list->type == J_ARR ? (int)list->count : 0;
    in->tilesets = (ta_tileset *)arena_alloc(&in->arena, sizeof(ta_tileset) * (size_t)(map->tileset_count + 1));
    if (!in->tilesets)
        goto no_memory_in;
    map->tilesets = in->tilesets;
    i = 0;
    for (item = list->type == J_ARR ? list->child : NULL; item; item = item->next, i++) {
        ta_tileset *tileset = &in->tilesets[i];
        tileset->first_gid = (uint32_t)j_int(j_get(item, "firstgid"), 1);
        tileset->name = "";
        tileset->image = "";
        tileset->tile_width = map->tile_width;
        tileset->tile_height = map->tile_height;
        if (j_has(item, "source")) {
            /* The file is the host's to read: ta_map_load_tileset. Until then
             * the name is the file's, without folder or extension. */
            const char *source = j_text(&in->arena, j_get(item, "source"), "");
            const char *start = source, *scan, *dot = NULL;
            for (scan = source; *scan; scan++) {
                if (*scan == '/' || *scan == '\\') {
                    start = scan + 1;
                    dot = NULL;
                } else if (*scan == '.') {
                    dot = scan;
                }
            }
            tileset->source = source;
            tileset->name = arena_string(&in->arena, start, (size_t)((dot ? dot : scan) - start));
        } else {
            read_tileset_fields(map, item, tileset);
        }
    }

    /* --- layers --- */
    list = j_get(root, "layers");
    map->layer_count = list->type == J_ARR ? (int)list->count : 0;
    in->layers = (ta_layer *)arena_alloc(&in->arena, sizeof(ta_layer) * (size_t)(map->layer_count + 1));
    if (!in->layers)
        goto no_memory_in;
    map->layers = in->layers;
    i = 0;
    for (item = list->type == J_ARR ? list->child : NULL; item; item = item->next, i++) {
        ta_layer *layer = &in->layers[i];
        const char *type = j_text(&in->arena, j_get(item, "type"), "tilelayer");
        const char *depth = j_text(&in->arena, j_get(item, "depth"), "");
        int32_t opacity = j_fixed(j_get(item, "opacity"), TA_ONE);
        layer->name = j_text(&in->arena, j_get(item, "name"), "");
        layer->class_name = j_text(&in->arena, j_get(item, "class"), "");
        layer->visible = j_bool(j_get(item, "visible"), 1);
        layer->opacity = clamp_int((int)(((int64_t)opacity * 255 + 32768) >> 16), 0, 255);
        layer->offset_x = j_fixed(j_get(item, "offsetx"), 0) / TA_ONE;
        layer->offset_y = j_fixed(j_get(item, "offsety"), 0) / TA_ONE;
        layer->depth = strcmp(depth, "sorted") == 0 ? TA_DEPTH_SORTED
                     : strcmp(depth, "above") == 0 ? TA_DEPTH_ABOVE : TA_DEPTH_BELOW;
        layer->width = map->width;
        layer->height = map->height;
        read_properties(&in->arena, j_get(item, "properties"), &layer->property_count, &layer->properties);

        if (strcmp(type, "tilelayer") == 0) {
            const int width = j_int(j_get(item, "width"), map->width);
            const int height = j_int(j_get(item, "height"), map->height);
            const size_t cells = (size_t)(width > 0 ? width : 0) * (size_t)(height > 0 ? height : 0);
            const char *problem = NULL;
            uint32_t *data;
            layer->type = TA_LAYER_TILE;
            if (cells > 16u * 1024u * 1024u) {
                set_error(error, error_size, "A layer is too large.", NULL);
                goto fail_in;
            }
            data = decode_layer_data(map, item, cells, &problem);
            if (!data) {
                set_error(error, error_size, "The map cannot be used: ", problem);
                goto fail_in;
            }
            if (width != map->width || height != map->height) {
                /* Fitted to the map, top left kept. */
                uint32_t *fitted = (uint32_t *)arena_alloc(&in->arena, sizeof(uint32_t) * (size_t)map->width * (size_t)map->height);
                int x, y;
                if (!fitted)
                    goto no_memory_in;
                for (y = 0; y < map->height && y < height; y++)
                    for (x = 0; x < map->width && x < width; x++)
                        fitted[(size_t)y * (size_t)map->width + (size_t)x] = data[(size_t)y * (size_t)width + (size_t)x];
                data = fitted;
            }
            layer->data = data;
            if (j_has(item, "animations"))
                read_animations(map, i, j_get(item, "animations"), layer, &pending);
        } else if (strcmp(type, "objectgroup") == 0) {
            layer->type = TA_LAYER_OBJECT;
            if (!read_objects(&in->arena, j_get(item, "objects"), layer))
                goto no_memory_in;
        } else if (strcmp(type, "imagelayer") == 0) {
            layer->type = TA_LAYER_IMAGE;
        } else {
            layer->type = TA_LAYER_GROUP;
        }
    }
    if (in->arena.failed)
        goto no_memory_in;

    /* --- layer groups: a name, and its layers in the map's own order --- */
    list = j_get(extension, "groups");
    if (list->type == J_ARR && list->count) {
        ta_group *groups = (ta_group *)arena_alloc(&in->arena, sizeof(ta_group) * list->count);
        int g = 0;
        if (!groups)
            goto no_memory_in;
        for (item = list->child; item; item = item->next) {
            const jnode *members = j_get(item, "layers"), *member;
            const char *name = j_text(&in->arena, j_get(item, "name"), "");
            int *indices, count = 0, known;
            if (!*name)
                continue;
            for (known = 0; known < g; known++)
                if (strcmp(groups[known].name, name) == 0)
                    break;
            if (known < g)
                continue; /* a name used twice: the first stands */
            indices = (int *)arena_alloc(&in->arena, sizeof(int) * (size_t)(map->layer_count + 1));
            if (!indices)
                goto no_memory_in;
            for (i = 0; i < map->layer_count; i++) {
                int listed = 0;
                if (members->type == J_ARR)
                    for (member = members->child; member && !listed; member = member->next)
                        listed = member->type == J_STR && strcmp(member->text, map->layers[i].name) == 0;
                if (listed)
                    indices[count++] = i;
            }
            groups[g].name = name;
            groups[g].count = count;
            groups[g].layers = indices;
            g++;
        }
        map->group_count = g;
        map->groups = groups;
    }

    /* --- names to indices, now that everything has one --- */
    for (; pending; pending = pending->next)
        pending->track->source = layer_by_name(map, pending->name);

    /* --- the tileset each layer keeps to --- */
    for (i = 0; i < map->layer_count; i++)
        in->layers[i].tileset = own_tileset(map, &in->layers[i]);
    {
        /* The tiles a swap track brings in count as the layer's own. Worked
         * out from the layers' own tiles, so the order does not matter. */
        int *shown = (int *)arena_alloc(&scratch, sizeof(int) * (size_t)(map->layer_count + 1));
        for (i = 0; shown && i < map->layer_count; i++) {
            const ta_layer *layer = &in->layers[i];
            int found = layer->tileset, n, t;
            for (n = 0; n < layer->animation_count; n++) {
                for (t = 0; t < layer->animations[n].track_count; t++) {
                    const ta_track *track = &layer->animations[n].tracks[t];
                    int theirs;
                    if (track->kind != TA_TRACK_SWAP || track->source < 0 || track->source == i)
                        continue;
                    theirs = in->layers[track->source].tileset;
                    if (theirs == TA_TILESET_NONE || found == TA_TILESET_MIXED)
                        continue;
                    found = found == TA_TILESET_NONE || found == theirs ? theirs : TA_TILESET_MIXED;
                }
            }
            shown[i] = found;
        }
        for (i = 0; shown && i < map->layer_count; i++)
            in->layers[i].tileset = shown[i];
    }
    for (i = 0; i < map->event_count; i++) {
        ta_action *actions = (ta_action *)in->events[i].actions;
        for (a = 0; a < in->events[i].action_count; a++) {
            const char *name = action_layers[actions[a].layer];
            const char *group = action_groups[actions[a].layer];
            if (*group)
                actions[a].group = ta_map_group(map, group);
            if (actions[a].kind == TA_DO_PALETTE) {
                int p;
                actions[a].layer = -1;
                actions[a].palette = -1; /* a name the map does not have is its own palette */
                for (p = 0; p < map->variant_count; p++)
                    if (strcmp(map->variants[p].name, actions[a].animation) == 0)
                        actions[a].palette = p; /* the last of a name, as the editor */
                actions[a].animation = "";
            } else {
                /* A group that does not exist is nothing, not "the layer instead". */
                actions[a].layer = *group ? -1 : layer_by_name(map, name);
            }
        }
    }

    read_properties(&in->arena, j_get(root, "properties"), &map->property_count, &map->properties);

    /* --- where each animation, track and cycle keeps its running state --- */
    in->anim_base = (int *)arena_alloc(&in->arena, sizeof(int) * (size_t)(map->layer_count + 1));
    in->swap_offset = (int32_t *)arena_alloc(&in->arena, sizeof(int32_t) * (size_t)(map->layer_count + 1));
    if (!in->anim_base || !in->swap_offset)
        goto no_memory_in;
    for (i = 0; i < map->layer_count; i++) {
        in->anim_base[i] = in->anim_total;
        in->anim_total += map->layers[i].animation_count;
    }
    in->track_base = (int *)arena_alloc(&in->arena, sizeof(int) * (size_t)(in->anim_total + 1));
    if (!in->track_base)
        goto no_memory_in;
    for (i = 0; i < map->layer_count; i++) {
        const ta_layer *layer = &map->layers[i];
        int swaps = 0, t;
        for (a = 0; a < layer->animation_count; a++) {
            in->track_base[in->anim_base[i] + a] = in->track_total;
            in->track_total += layer->animations[a].track_count;
            for (t = 0; t < layer->animations[a].track_count; t++)
                swaps += layer->animations[a].tracks[t].kind == TA_TRACK_SWAP;
        }
        in->swap_offset[i] = -1;
        if (swaps && layer->type == TA_LAYER_TILE) {
            in->swap_offset[i] = (int32_t)in->swap_bytes;
            in->swap_bytes += (size_t)map->width * (size_t)map->height;
        }
    }
    if (in->arena.failed)
        goto no_memory_in;

    arena_free(&scratch);
    return map;

no_memory_in:
    arena = in->arena;
no_memory:
    set_error(error, error_size, "Out of memory.", NULL);
    arena_free(&scratch);
    arena_free(&arena);
    return NULL;
fail_in:
    arena = in->arena;
fail:
    arena_free(&scratch);
    arena_free(&arena);
    return NULL;
}

int ta_map_load_tileset(ta_map *map, int tileset, const char *json, size_t length, char *error, size_t error_size)
{
    ta_internal *in;
    const jnode *root;
    if (!map || tileset < 0 || tileset >= map->tileset_count)
        return 0;
    in = (ta_internal *)map->internal;
    /* The strings stay with the map, so the text is parsed into its own arena. */
    root = j_parse(&in->arena, json, length, error, error_size);
    if (!root)
        return 0;
    if (root->type != J_OBJ) {
        set_error(error, error_size, "This is not a JSON tileset. (XML tilesets, .tsx, are for the editor to import.)", NULL);
        return 0;
    }
    read_tileset_fields(map, root, &in->tilesets[tileset]);
    in->tilesets[tileset].source = NULL;
    return !in->arena.failed;
}

void ta_map_set_image_size(ta_map *map, int tileset, int width, int height)
{
    ta_tileset *t;
    int step_x, step_y, fit_columns, fit_rows;
    if (!map || tileset < 0 || tileset >= map->tileset_count)
        return;
    t = &((ta_internal *)map->internal)->tilesets[tileset];
    t->image_width = width;
    t->image_height = height;
    step_x = t->tile_width + t->spacing;
    step_y = t->tile_height + t->spacing;
    if (step_x < 1 || step_y < 1)
        return;
    fit_columns = (width - 2 * t->margin + t->spacing) / step_x;
    fit_rows = (height - 2 * t->margin + t->spacing) / step_y;
    if (fit_columns < 0) fit_columns = 0;
    if (fit_rows < 0) fit_rows = 0;
    if (t->columns <= 0 || t->columns > fit_columns)
        t->columns = fit_columns;
    if (t->tile_count <= 0)
        t->tile_count = t->columns * fit_rows;
}

void ta_map_free(ta_map *map)
{
    ta_arena arena;
    if (!map)
        return;
    arena = ((ta_internal *)map->internal)->arena;
    arena_free(&arena);
}

int ta_map_event(const ta_map *map, const char *name)
{
    int i;
    if (!map || !name)
        return -1;
    for (i = 0; i < map->event_count; i++)
        if (strcmp(map->events[i].name, name) == 0)
            return i;
    return -1;
}

int ta_map_layer(const ta_map *map, const char *name)
{
    return map && name ? layer_by_name(map, name) : -1;
}

int ta_map_tileset(const ta_map *map, const char *name)
{
    int i;
    if (!map || !name)
        return -1;
    for (i = 0; i < map->tileset_count; i++)
        if (strcmp(map->tilesets[i].name, name) == 0)
            return i;
    return -1;
}

int ta_map_group(const ta_map *map, const char *name)
{
    int i;
    if (!map || !name)
        return -1;
    for (i = 0; i < map->group_count; i++)
        if (strcmp(map->groups[i].name, name) == 0)
            return i;
    return -1;
}

int ta_map_tileset_of(const ta_map *map, uint32_t gid)
{
    int i, best = -1;
    gid &= TA_GID_MASK;
    if (gid == 0)
        return -1;
    /* The owner is the tileset with the highest first id not above this one. */
    for (i = 0; i < map->tileset_count; i++)
        if (map->tilesets[i].first_gid <= gid && (best < 0 || map->tilesets[i].first_gid > map->tilesets[best].first_gid))
            best = i;
    return best;
}

void ta_tileset_tile(const ta_tileset *tileset, uint32_t gid, int *x, int *y)
{
    const int index = (int)((gid & TA_GID_MASK) - tileset->first_gid);
    const int columns = tileset->columns > 0 ? tileset->columns : 1;
    *x = tileset->margin + (index % columns) * (tileset->tile_width + tileset->spacing);
    *y = tileset->margin + (index / columns) * (tileset->tile_height + tileset->spacing);
}

/* ============================================================== evaluation */

/* n / d rounded down, for a positive d and an n of either sign. */
static int32_t div_floor(int32_t n, int32_t d)
{
    int32_t q = n / d;
    return (n % d != 0 && n < 0) ? q - 1 : q;
}

/* n / d to nearest, halves up. floor((2n + d) / 2d) without the doubling:
 * the half of an odd d that integer division drops never reaches the next
 * whole number, so it is the same thing in 32 bits. */
static int32_t div_round(int32_t n, int32_t d)
{
    return div_floor(n + d / 2, d);
}

static int lerp_round(int a, int b, int32_t p)
{
    int64_t product = (int64_t)(b - a) * (int64_t)p;
    return a + (int)((product + 32768) >> 16);
}

static int lerp_floor(int a, int b, int32_t p)
{
    int64_t product = (int64_t)(b - a) * (int64_t)p;
    return a + (int)(product >> 16);
}

typedef struct ta_shape {
    int delay, duration, loop, pingpong, ease;
} ta_shape;

typedef struct ta_pose {
    int x, y, alpha, cel, rate, period, on;
} ta_pose;

/* docs/FORMAT.md 6.1. */
static int local_time(const ta_shape *shape, int32_t elapsed, int32_t *time)
{
    const int duration = clamp_int(shape->duration, 1, 32767);
    const int32_t e = elapsed - shape->delay;
    int32_t pass, within, u, p;
    int reverse;

    if (e < 0)
        return 0;
    pass = e / duration;
    within = e % duration;
    reverse = shape->pingpong && (pass & 1);

    if (!shape->loop && pass >= (shape->pingpong ? 2 : 1)) {
        *time = shape->pingpong ? 0 : duration * TA_ONE;
        return 1;
    }
    if (shape->ease == 0) {
        *time = (reverse ? duration - within : within) * TA_ONE;
        return 1;
    }
    u = (within * TA_ONE) / duration;
    if (reverse)
        u = TA_ONE - u;
    p = clamp_int(TA_EASE(shape->ease, u), 0, TA_ONE);
    *time = p * duration;
    return 1;
}

static int fit_cel(const ta_track *track, int cel)
{
    const int count = track->kind == TA_TRACK_BLIT ? (track->frames > 1 ? track->frames : 1) : 1;
    if (track->cycle) {
        cel %= count;
        return cel < 0 ? cel + count : cel;
    }
    return clamp_int(cel, 0, count - 1);
}

static void pose_from_key(const ta_track *track, const ta_key *key, ta_pose *pose)
{
    pose->x = key->x;
    pose->y = key->y;
    pose->alpha = clamp_int(key->alpha, 0, 255);
    pose->cel = fit_cel(track, key->cel);
    pose->rate = key->rate < 0 ? 0 : key->rate;
    pose->period = key->period < 1 ? 1 : key->period;
    pose->on = clamp_int(key->on, 0, pose->period);
}

/* docs/FORMAT.md 6.2. */
static void eval_track(const ta_track *track, int32_t time, ta_pose *pose)
{
    const ta_key *a, *b;
    int index = -1, i, span;
    int32_t into;

    pose->x = 0;
    pose->y = 0;
    pose->alpha = 255;
    pose->cel = 0;
    pose->rate = 0;
    pose->period = 1;
    pose->on = 0;
    if (track->key_count == 0)
        return;

    for (i = 0; i < track->key_count; i++) {
        /* key.f <= 32767, so f * ONE fits. */
        if (track->keys[i].f * TA_ONE > time)
            break;
        index = i;
    }
    if (index < 0) {
        pose_from_key(track, &track->keys[0], pose);
        return;
    }
    a = &track->keys[index];
    if (index + 1 >= track->key_count || !a->tween) {
        pose_from_key(track, a, pose);
        return;
    }
    b = &track->keys[index + 1];
    span = b->f - a->f;
    into = time - a->f * TA_ONE;

    if (a->ease == 0 && (into & (TA_ONE - 1)) == 0) {
        const int32_t m = into >> 16;
        pose->x = a->x + div_round((b->x - a->x) * m, span);
        pose->y = a->y + div_round((b->y - a->y) * m, span);
        pose->alpha = clamp_int(a->alpha + div_round((b->alpha - a->alpha) * m, span), 0, 255);
        pose->cel = fit_cel(track, a->cel + div_floor((b->cel - a->cel) * m, span));
        pose->rate = a->rate + div_round((b->rate - a->rate) * m, span);
        if (pose->rate < 0) pose->rate = 0;
        pose->period = a->period + div_round((b->period - a->period) * m, span);
        if (pose->period < 1) pose->period = 1;
        pose->on = clamp_int(a->on + div_round((b->on - a->on) * m, span), 0, pose->period);
        return;
    }

    {
        const int32_t t = into / span;
        const int32_t p = TA_EASE(a->ease, t);
        pose->x = lerp_round(a->x, b->x, p);
        pose->y = lerp_round(a->y, b->y, p);
        pose->alpha = clamp_int(lerp_round(a->alpha, b->alpha, p), 0, 255);
        pose->cel = fit_cel(track, lerp_floor(a->cel, b->cel, p));
        pose->rate = lerp_round(a->rate, b->rate, p);
        if (pose->rate < 0) pose->rate = 0;
        pose->period = lerp_round(a->period, b->period, p);
        if (pose->period < 1) pose->period = 1;
        pose->on = clamp_int(lerp_round(a->on, b->on, p), 0, pose->period);
    }
}

/* ============================================================== the player */

typedef struct st_head {
    int32_t origin; /* the clock at reset */
    int32_t frame;
} st_head;

#define ST_HAS_DELAY 1
#define ST_HAS_DURATION 2
#define ST_HAS_LOOP 4
#define ST_HAS_PINGPONG 8
#define ST_HAS_EASE 16

typedef struct st_anim {
    int32_t start;     /* the frame its clock read 0 */
    int32_t paused_at; /* its clock, while paused */
    int32_t delay, duration;
    uint8_t status, finished, has, loop, pingpong, ease, pad[2];
} st_anim;

/* A drifting layer track: where it has got to and the wind it is between. */
typedef struct st_drift {
    int32_t x, y;           /* 1/256 pixel */
    int32_t from_x, from_y; /* velocity at the last change */
    int32_t to_x, to_y;     /* velocity at the next */
    int32_t count;          /* frames drifted */
    int32_t pad;
} st_drift;

typedef struct st_cycle {
    int32_t start;
    int32_t running;
} st_cycle;

struct ta_player {
    const ta_map *map;
    const ta_internal *in;
    ta_allocator allocator;
    uint8_t *state;
    size_t state_size;
    st_head *head;
    st_anim *anims;
    uint32_t *random;   /* one generator per track */
    st_drift *drift;    /* per track */
    uint32_t seed;      /* mixed into every generator */
    st_cycle *cycles;
    int32_t *palette;   /* the variant in force: -1 = the map's own */
    int8_t *visible;    /* per layer: -1 as the map says, 0 hidden, 1 shown */
    uint8_t *swapped;   /* per cell of each layer with swap tracks: 0, or the source layer + 1 */
    int *finished;      /* scratch for one step */
    int32_t age;        /* of the event being fired */
    int auto_swaps;
};

#define MAX_EVENT_DEPTH 8

ta_player *ta_player_new(const ta_map *map, const ta_allocator *given)
{
    ta_allocator allocator;
    const ta_internal *in;
    ta_player *player;
    size_t at = 0, total;
    size_t o_head, o_anims, o_random, o_drift, o_cycles, o_palettes, o_visible, o_swapped;

    if (!map)
        return NULL;
    in = (const ta_internal *)map->internal;
    if (given && given->alloc && given->release)
        allocator = *given;
    else
        allocator = in->arena.allocator;

#define TA_PLACE(offset, bytes) do { offset = at; at += ((bytes) + 7u) & ~(size_t)7u; } while (0)
    TA_PLACE(o_head, sizeof(st_head));
    TA_PLACE(o_anims, sizeof(st_anim) * (size_t)in->anim_total);
    TA_PLACE(o_random, sizeof(uint32_t) * (size_t)in->track_total);
    TA_PLACE(o_drift, sizeof(st_drift) * (size_t)in->track_total);
    TA_PLACE(o_cycles, sizeof(st_cycle) * (size_t)map->cycle_count);
    TA_PLACE(o_palettes, sizeof(int32_t));
    TA_PLACE(o_visible, (size_t)map->layer_count);
    TA_PLACE(o_swapped, in->swap_bytes);
#undef TA_PLACE
    total = at;

    player = (ta_player *)allocator.alloc(allocator.user, sizeof(ta_player));
    if (!player)
        return NULL;
    memset(player, 0, sizeof *player);
    player->state = (uint8_t *)allocator.alloc(allocator.user, total ? total : 8);
    player->finished = (int *)allocator.alloc(allocator.user, sizeof(int) * (size_t)(in->anim_total + 1));
    if (!player->state || !player->finished) {
        if (player->state) allocator.release(allocator.user, player->state);
        if (player->finished) allocator.release(allocator.user, player->finished);
        allocator.release(allocator.user, player);
        return NULL;
    }
    player->map = map;
    player->in = in;
    player->allocator = allocator;
    player->state_size = total;
    player->head = (st_head *)(void *)(player->state + o_head);
    player->anims = (st_anim *)(void *)(player->state + o_anims);
    player->random = (uint32_t *)(void *)(player->state + o_random);
    player->drift = (st_drift *)(void *)(player->state + o_drift);
    player->cycles = (st_cycle *)(void *)(player->state + o_cycles);
    player->palette = (int32_t *)(void *)(player->state + o_palettes);
    player->visible = (int8_t *)(player->state + o_visible);
    player->swapped = player->state + o_swapped;
    player->auto_swaps = 1;
    ta_player_reset(player, 0);
    return player;
}

void ta_player_free(ta_player *player)
{
    ta_allocator allocator;
    if (!player)
        return;
    allocator = player->allocator;
    allocator.release(allocator.user, player->state);
    allocator.release(allocator.user, player->finished);
    allocator.release(allocator.user, player);
}

size_t ta_player_state_size(const ta_player *player)
{
    return player->state_size;
}

void ta_player_save(const ta_player *player, void *out)
{
    memcpy(out, player->state, player->state_size);
}

void ta_player_load(ta_player *player, const void *state)
{
    memcpy(player->state, state, player->state_size);
}

void ta_player_set_seed(ta_player *player, uint32_t seed)
{
    player->seed = seed;
}

void ta_player_set_auto_swaps(ta_player *player, int enabled)
{
    player->auto_swaps = enabled != 0;
}

int32_t ta_player_frame(const ta_player *player)
{
    return player->head->frame < player->head->origin ? player->head->origin : player->head->frame;
}

static st_anim *run_of(const ta_player *player, int layer, int animation)
{
    return &player->anims[player->in->anim_base[layer] + animation];
}

static uint32_t *random_of(const ta_player *player, int layer, int animation)
{
    return &player->random[player->in->track_base[player->in->anim_base[layer] + animation]];
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

/* One axis of a new wind: anything from -speed to +speed. */
static int32_t drift_pick(uint32_t *state, int speed)
{
    return (int32_t)(next_random(state) % (uint32_t)(2 * speed + 1)) - speed;
}

static st_drift *drift_of(const ta_player *player, int layer, int animation)
{
    return &player->drift[player->in->track_base[player->in->anim_base[layer] + animation]];
}

static void start_animation(ta_player *player, int layer, int animation)
{
    const ta_animation *anim = &player->map->layers[layer].animations[animation];
    st_anim *run = run_of(player, layer, animation);
    uint32_t *random = random_of(player, layer, animation);
    st_drift *drift = drift_of(player, layer, animation);
    int t;
    run->status = TA_PLAYING;
    run->start = ta_player_frame(player) - player->age;
    run->paused_at = 0;
    run->finished = 0;
    for (t = 0; t < anim->track_count; t++) {
        const ta_track *track = &anim->tracks[t];
        random[t] = track->seed + (uint32_t)(t + 1) * 0x9E3779B9u + player->seed * 0x85EBCA6Bu;
        memset(&drift[t], 0, sizeof drift[t]);
        /* A drifting layer sets off on a wind already blowing. */
        if (track->kind == TA_TRACK_LAYER && track->drift_speed > 0) {
            drift[t].to_x = drift_pick(&random[t], track->drift_speed);
            drift[t].to_y = drift_pick(&random[t], track->drift_speed);
        }
    }
}

void ta_player_reset(ta_player *player, int32_t clock)
{
    const ta_map *map = player->map;
    int i, a, c;
    memset(player->state, 0, player->state_size);
    player->head->origin = clock < 0 ? 0 : clock;
    player->head->frame = player->head->origin - 1;
    player->age = 0;
    *player->palette = -1;
    for (i = 0; i < map->layer_count; i++) {
        player->visible[i] = -1;
        for (a = 0; a < map->layers[i].animation_count; a++) {
            if (map->layers[i].animations[a].trigger == TA_TRIGGER_ALWAYS) {
                start_animation(player, i, a);
                /* Since frame 0 of the clock, whenever the map was started. */
                run_of(player, i, a)->start = 0;
            }
        }
    }
    for (c = 0; c < map->cycle_count; c++) {
        if (map->cycles[c].trigger == TA_TRIGGER_ALWAYS) {
            player->cycles[c].running = 1;
            player->cycles[c].start = 0;
        }
    }
}

static void shape_of(const ta_animation *anim, const st_anim *run, ta_shape *shape)
{
    shape->delay = (run->has & ST_HAS_DELAY) ? run->delay : anim->delay;
    shape->duration = clamp_int((run->has & ST_HAS_DURATION) ? run->duration : anim->duration, 1, 32767);
    shape->loop = (run->has & ST_HAS_LOOP) ? run->loop : anim->loop;
    shape->pingpong = (run->has & ST_HAS_PINGPONG) ? run->pingpong : anim->pingpong;
    shape->ease = (run->has & ST_HAS_EASE) ? run->ease : anim->ease;
}

static int32_t clock_of(const ta_player *player, const st_anim *run)
{
    return run->status == TA_PAUSED ? run->paused_at : ta_player_frame(player) - run->start;
}

static int time_of(const ta_player *player, const ta_animation *anim, const st_anim *run, int32_t *out)
{
    ta_shape shape;
    if (run->status == TA_STOPPED)
        return 0;
    shape_of(anim, run, &shape);
    return local_time(&shape, clock_of(player, run), out);
}

static int region_holds(const ta_track *track, const ta_layer *layer, int cx, int cy)
{
    const int w = track->src_w > 0 ? track->src_w : layer->width;
    const int h = track->src_h > 0 ? track->src_h : layer->height;
    return cx >= track->src_x && cy >= track->src_y && cx < track->src_x + w && cy < track->src_y + h;
}

static uint32_t cell_of(const ta_layer *layer, int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= layer->width || cy >= layer->height || !layer->data)
        return 0;
    return layer->data[(size_t)cy * (size_t)layer->width + (size_t)cx];
}

/* A swap track that can do anything: it names another tile layer. */
static int swap_usable(const ta_map *map, int layer, const ta_track *track)
{
    return track->kind == TA_TRACK_SWAP && !track->muted && track->source >= 0 && track->source != layer
        && track->source < 255 && map->layers[track->source].type == TA_LAYER_TILE;
}

/* An animation that is playing and has not run out: 1, with its time. */
static int animation_live(const ta_player *player, int layer, int animation, int32_t *now)
{
    const ta_animation *anim = &player->map->layers[layer].animations[animation];
    const st_anim *run = run_of(player, layer, animation);
    ta_shape shape;
    int32_t tick;
    if (run->status != TA_PLAYING)
        return 0;
    shape_of(anim, run, &shape);
    tick = clock_of(player, run) - shape.delay;
    if (tick < 0 || run->finished)
        return 0;
    return time_of(player, anim, run, now);
}

static void fire_now(ta_player *player, int event, int depth);

/* One frame: swap tracks make their picks, and animations that have just run
 * out fire their "then" event. docs/FORMAT.md 6 and 6.3. */
static void step(ta_player *player)
{
    const ta_map *map = player->map;
    int finished = 0, l, a, t, i;

    for (l = 0; l < map->layer_count; l++) {
        const ta_layer *layer = &map->layers[l];
        uint8_t *swapped = player->in->swap_offset[l] >= 0 ? player->swapped + player->in->swap_offset[l] : NULL;
        for (a = 0; a < layer->animation_count; a++) {
            const ta_animation *anim = &layer->animations[a];
            st_anim *run = run_of(player, l, a);
            uint32_t *random = random_of(player, l, a);
            ta_shape shape;
            int32_t tick, now = 0;

            if (run->status != TA_PLAYING)
                continue;
            shape_of(anim, run, &shape);
            tick = clock_of(player, run) - shape.delay;
            if (tick < 0)
                continue;
            if (!shape.loop && !run->finished && tick >= shape.duration * (shape.pingpong ? 2 : 1)) {
                run->finished = 1;
                if (anim->then >= 0)
                    player->finished[finished++] = anim->then;
            }
            /* A finished animation rests; it no longer picks cells or drifts. */
            if (run->finished)
                continue;
            if (!time_of(player, anim, run, &now))
                continue;

            for (t = 0; t < anim->track_count; t++) {
                const ta_track *track = &anim->tracks[t];
                const ta_layer *source;
                ta_pose pose;
                int x0, y0, w, h, pick;
                if (track->kind == TA_TRACK_LAYER && track->drift_speed > 0 && !track->muted) {
                    /* The wind eases from one random velocity to the next,
                     * and the layer goes where it blows. FORMAT.md 7.4. */
                    st_drift *drift = &drift_of(player, l, a)[t];
                    const int change = track->drift_change > 1 ? track->drift_change : 1;
                    const int32_t k = drift->count % change;
                    if (k == 0) {
                        drift->from_x = drift->to_x;
                        drift->from_y = drift->to_y;
                        drift->to_x = drift_pick(&random[t], track->drift_speed);
                        drift->to_y = drift_pick(&random[t], track->drift_speed);
                    }
                    drift->x += drift->from_x + div_round((drift->to_x - drift->from_x) * k, change);
                    drift->y += drift->from_y + div_round((drift->to_y - drift->from_y) * k, change);
                    drift->x %= map->width * map->tile_width * 256;
                    drift->y %= map->height * map->tile_height * 256;
                    drift->count = (drift->count + 1) % (change * 2);
                    continue;
                }
                if (!player->auto_swaps || !swapped || !swap_usable(map, l, track))
                    continue;
                source = &map->layers[track->source];

                /* The region, clipped to the layer; none given means all of it. */
                x0 = clamp_int(track->src_x, 0, layer->width);
                y0 = clamp_int(track->src_y, 0, layer->height);
                w = track->src_w > 0 ? track->src_w : layer->width;
                h = track->src_h > 0 ? track->src_h : layer->height;
                if (w > layer->width - x0) w = layer->width - x0;
                if (h > layer->height - y0) h = layer->height - y0;
                if (w <= 0 || h <= 0)
                    continue;

                eval_track(track, now, &pose);
                for (pick = 0; pick < pose.rate; pick++) {
                    const uint32_t r = next_random(&random[t]) % (uint32_t)(w * h);
                    const int cx = x0 + (int)(r % (uint32_t)w);
                    const int cy = y0 + (int)(r / (uint32_t)w);
                    ta_pose rhythm = pose;
                    uint8_t *cell;
                    int later, want;
                    /* Only cells that have a tile in both layers take part. */
                    if (cell_of(layer, cx, cy) == 0 || cell_of(source, cx, cy) == 0)
                        continue;
                    /* The rhythm is the cell's: that of the animation's last
                     * swap track whose region holds it, whichever track picked. */
                    for (later = anim->track_count - 1; later > t; later--) {
                        const ta_track *other = &anim->tracks[later];
                        if (other->kind == TA_TRACK_SWAP && !other->muted && region_holds(other, layer, cx, cy)) {
                            eval_track(other, now, &rhythm);
                            break;
                        }
                    }
                    want = (ta_player_frame(player) % rhythm.period) >= rhythm.period - rhythm.on;
                    cell = &swapped[(size_t)cy * (size_t)layer->width + (size_t)cx];
                    if (want && *cell == 0)
                        *cell = (uint8_t)(track->source + 1);
                    else if (!want && *cell != 0)
                        *cell = 0;
                }
            }
        }
    }

    for (i = 0; i < finished; i++)
        fire_now(player, player->finished[i], 1);
}

void ta_player_advance(ta_player *player, int32_t frame)
{
    while (player->head->frame < frame) {
        player->head->frame++;
        step(player);
    }
}

static void apply_to_layer(ta_player *player, const ta_action *action, int layer_index);

static void apply_action(ta_player *player, const ta_action *action, int depth)
{
    const ta_map *map = player->map;
    int i;

    if (action->kind == TA_DO_FIRE) {
        fire_now(player, action->event, depth + 1);
        return;
    }
    if (action->kind == TA_DO_PALETTE) {
        *player->palette = action->palette;
        return;
    }
    if (action->kind == TA_DO_NOTHING)
        return;
    /* On every layer of its group, or on its one layer. */
    if (action->group >= 0) {
        const ta_group *group = &map->groups[action->group];
        for (i = 0; i < group->count; i++)
            apply_to_layer(player, action, group->layers[i]);
    } else if (action->layer >= 0) {
        apply_to_layer(player, action, action->layer);
    }
}

static void apply_to_layer(ta_player *player, const ta_action *action, int layer_index)
{
    const ta_layer *layer = &player->map->layers[layer_index];
    int a;

    if (action->kind == TA_DO_SHOW || action->kind == TA_DO_HIDE || action->kind == TA_DO_TOGGLE) {
        const int shown = player->visible[layer_index] < 0 ? layer->visible : player->visible[layer_index] != 0;
        player->visible[layer_index] = (int8_t)(action->kind == TA_DO_SHOW ? 1 : action->kind == TA_DO_HIDE ? 0 : !shown);
        return;
    }
    if (action->kind == TA_DO_RESTORE) {
        ta_player_swap_restore(player, layer_index);
        return;
    }

    for (a = 0; a < layer->animation_count; a++) {
        const ta_animation *anim = &layer->animations[a];
        st_anim *run;
        if (!(action->animation[0] == '*' && action->animation[1] == 0) && strcmp(anim->id, action->animation) != 0)
            continue;
        run = run_of(player, layer_index, a);

        switch (action->kind) {
        case TA_DO_PLAY:
            if (run->status == TA_PAUSED) {
                /* Carry on from where it was paused. */
                run->status = TA_PLAYING;
                run->start = ta_player_frame(player) - run->paused_at - player->age;
            } else {
                start_animation(player, layer_index, a);
            }
            break;
        case TA_DO_STOP:
            run->status = TA_STOPPED;
            break;
        case TA_DO_PAUSE:
            if (run->status == TA_PLAYING) {
                run->paused_at = ta_player_frame(player) - run->start;
                run->status = TA_PAUSED;
            }
            break;
        case TA_DO_REWIND:
            run->start = ta_player_frame(player);
            run->paused_at = 0;
            run->finished = 0;
            break;
        case TA_DO_SET:
            switch (action->attribute) {
            case TA_SET_DELAY: {
                int32_t value = action->has_number ? action->number : anim->delay;
                run->delay = value < 0 ? 0 : value;
                run->has |= ST_HAS_DELAY;
                break;
            }
            case TA_SET_DURATION:
                run->duration = clamp_int(action->has_number ? action->number : anim->duration, 1, 32767);
                run->has |= ST_HAS_DURATION;
                break;
            case TA_SET_LOOP:
                run->loop = (uint8_t)action->flag;
                run->has |= ST_HAS_LOOP;
                break;
            case TA_SET_PINGPONG:
                run->pingpong = (uint8_t)action->flag;
                run->has |= ST_HAS_PINGPONG;
                break;
            case TA_SET_EASE:
                run->ease = (uint8_t)(action->ease >= 0 ? action->ease : anim->ease);
                run->has |= ST_HAS_EASE;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

static void fire_now(ta_player *player, int event, int depth)
{
    const ta_map *map = player->map;
    const ta_event *definition;
    int l, a, i, c;

    /* Events may fire events; a loop of them must not hang the program. */
    if (event < 0 || event >= map->event_count || depth > MAX_EVENT_DEPTH)
        return;

    /* What the event triggers by name... */
    for (l = 0; l < map->layer_count; l++)
        for (a = 0; a < map->layers[l].animation_count; a++)
            if (map->layers[l].animations[a].trigger == event)
                start_animation(player, l, a);
    for (c = 0; c < map->cycle_count; c++) {
        if (map->cycles[c].trigger == event) {
            player->cycles[c].running = 1;
            player->cycles[c].start = ta_player_frame(player) - player->age;
        }
    }

    /* ...then what it was told to do. */
    definition = &map->events[event];
    for (i = 0; i < definition->action_count; i++)
        apply_action(player, &definition->actions[i], depth);
}

void ta_player_fire(ta_player *player, int event, int32_t age)
{
    player->age = age < 0 ? 0 : age;
    fire_now(player, event, 0);
    player->age = 0;
}

/* ------------------------------------------------------------ what to draw */

int ta_player_layer_visible(const ta_player *player, int layer)
{
    if (layer < 0 || layer >= player->map->layer_count)
        return 0;
    if (player->visible[layer] >= 0)
        return player->visible[layer] != 0;
    return player->map->layers[layer].visible;
}

const ta_object *ta_player_object(const ta_player *player, const char *name, int *x, int *y, int *layer)
{
    const ta_map *map = player->map;
    int l, o;
    for (l = map->layer_count - 1; l >= 0; l--) {
        const ta_layer *candidate = &map->layers[l];
        if (candidate->type != TA_LAYER_OBJECT || !ta_player_layer_visible(player, l))
            continue;
        for (o = 0; o < candidate->object_count; o++) {
            const ta_object *object = &candidate->objects[o];
            if (!object->visible || strcmp(object->name, name) != 0)
                continue;
            if (x)
                *x = object->x + candidate->offset_x;
            if (y)
                *y = object->y + candidate->offset_y;
            if (layer)
                *layer = l;
            return object;
        }
    }
    return NULL;
}

const ta_property *ta_property_find(const ta_property *properties, int count, const char *name)
{
    int i;
    for (i = 0; i < count; i++)
        if (strcmp(properties[i].name, name) == 0)
            return &properties[i];
    return NULL;
}

int ta_player_status(const ta_player *player, int layer, int animation)
{
    if (layer < 0 || layer >= player->map->layer_count || animation < 0
        || animation >= player->map->layers[layer].animation_count)
        return TA_STOPPED;
    return run_of(player, layer, animation)->status;
}

int32_t ta_player_clock(const ta_player *player, int layer, int animation)
{
    if (ta_player_status(player, layer, animation) == TA_STOPPED)
        return -1;
    return clock_of(player, run_of(player, layer, animation));
}

/* The layer tracks summed, and optionally the blits collected. */
static int pose_layer(const ta_player *player, int layer_index, ta_layer_pose *pose, ta_blit *blits, int max)
{
    const ta_map *map = player->map;
    const ta_layer *layer;
    int a, t, count = 0;
    int offset_x = 0, offset_y = 0, alpha = 255, wrap = 0;

    if (layer_index < 0 || layer_index >= map->layer_count) {
        if (pose)
            memset(pose, 0, sizeof *pose);
        return 0;
    }
    layer = &map->layers[layer_index];

    for (a = 0; a < layer->animation_count; a++) {
        const ta_animation *anim = &layer->animations[a];
        int32_t now = 0;
        if (!time_of(player, anim, run_of(player, layer_index, a), &now))
            continue;
        for (t = 0; t < anim->track_count; t++) {
            const ta_track *track = &anim->tracks[t];
            ta_pose track_pose;
            if (track->muted || track->kind == TA_TRACK_SWAP)
                continue;
            eval_track(track, now, &track_pose);

            if (track->kind == TA_TRACK_LAYER) {
                if (track->drift_speed > 0) {
                    /* Whole pixels, rounded down, so it never jitters about zero. */
                    const st_drift *drift = &drift_of(player, layer_index, a)[t];
                    offset_x += drift->x >= 0 ? drift->x / 256 : -((-drift->x + 255) / 256);
                    offset_y += drift->y >= 0 ? drift->y / 256 : -((-drift->y + 255) / 256);
                }
                offset_x += track_pose.x;
                offset_y += track_pose.y;
                alpha = alpha * track_pose.alpha / 255;
                wrap = wrap || track->wrap;
                continue;
            }
            if (track->source < 0)
                continue;
            if (blits && count < max) {
                ta_blit *blit = &blits[count];
                const int masked = track->mask_w > 0 && track->mask_h > 0;
                blit->source_layer = track->source;
                blit->src_x = track->src_x + track_pose.cel * track->stride;
                blit->src_y = track->src_y;
                blit->w = track->src_w;
                blit->h = track->src_h;
                blit->x = track->dst_x * map->tile_width + track_pose.x;
                blit->y = track->dst_y * map->tile_height + track_pose.y;
                blit->alpha = track_pose.alpha;
                blit->mask_x = masked ? track->mask_x : 0;
                blit->mask_y = masked ? track->mask_y : 0;
                blit->mask_w = masked ? track->mask_w : 0;
                blit->mask_h = masked ? track->mask_h : 0;
                /* What stands on the ground is the window, when there is one. */
                blit->anchor_x = (masked ? track->mask_x : blit->x) + track->anchor_x;
                blit->anchor_y = (masked ? track->mask_y : blit->y) + track->anchor_y;
                blit->animation = a;
                blit->track = t;
            }
            count++;
        }
    }
    if (pose) {
        pose->offset_x = layer->offset_x + offset_x;
        pose->offset_y = layer->offset_y + offset_y;
        pose->alpha = alpha * layer->opacity / 255;
        pose->wrap = wrap;
    }
    return count;
}

void ta_player_layer_pose(const ta_player *player, int layer, ta_layer_pose *out)
{
    pose_layer(player, layer, out, NULL, 0);
}

int ta_player_blits(const ta_player *player, int layer, ta_blit *out, int max)
{
    return pose_layer(player, layer, NULL, out, out ? max : 0);
}

uint32_t ta_player_gid(const ta_player *player, int layer, int cx, int cy)
{
    const ta_map *map = player->map;
    const ta_layer *own;
    if (layer < 0 || layer >= map->layer_count)
        return 0;
    own = &map->layers[layer];
    if (cx < 0 || cy < 0 || cx >= own->width || cy >= own->height || !own->data)
        return 0;
    if (player->in->swap_offset[layer] >= 0) {
        const uint8_t source = player->swapped[(size_t)player->in->swap_offset[layer] + (size_t)cy * (size_t)own->width + (size_t)cx];
        if (source)
            return cell_of(&map->layers[source - 1], cx, cy);
    }
    return own->data[(size_t)cy * (size_t)own->width + (size_t)cx];
}

int ta_player_palette_choice(const ta_player *player)
{
    return (int)*player->palette;
}

int ta_player_palette(const ta_player *player, ta_color *out)
{
    const ta_map *map = player->map;
    const int count = map->palette_count;
    const int choice = (int)*player->palette;
    ta_color chosen[256];
    int c, i;

    if (count <= 0)
        return 0;
    memcpy(out, map->palette, sizeof(ta_color) * (size_t)count);

    /* The variant an event chose, then the cycles turning within it. */
    if (choice >= 0 && choice < map->variant_count) {
        const ta_variant *variant = &map->variants[choice];
        for (i = 0; i < variant->count; i++) {
            const int entry = variant->entries[i];
            uint8_t alpha;
            if (entry >= count)
                continue;
            /* The transparent entry stays transparent whatever color it is given. */
            alpha = out[entry].a;
            out[entry] = variant->colors[i];
            out[entry].a = alpha;
        }
    }

    memcpy(chosen, out, sizeof(ta_color) * (size_t)count);
    for (c = 0; c < map->cycle_count; c++) {
        const ta_cycle *cycle = &map->cycles[c];
        const st_cycle *run = &player->cycles[c];
        const int first = cycle->first < 0 ? 0 : cycle->first;
        const int last = cycle->last >= count ? count - 1 : cycle->last;
        const int length = last - first + 1;
        const int32_t elapsed = ta_player_frame(player) - run->start;
        int shift;
        if (!run->running || length < 2 || elapsed < 0)
            continue;
        /* One step every `frames` frames; after `length` steps it is back where it began. */
        shift = (int)((elapsed / (cycle->frames < 1 ? 1 : cycle->frames)) % length);
        if (shift == 0)
            continue;
        for (i = 0; i < length; i++) {
            const int from = cycle->reverse ? (i + shift) % length : (i - shift + length) % length;
            out[first + i] = chosen[first + from];
        }
    }
    return count;
}

/* ----------------------------------------- swaps, when the game picks cells */

int ta_swap_wanted(const ta_swap_rhythm *rhythm, int32_t tick)
{
    const int period = rhythm->period < 1 ? 1 : rhythm->period;
    int32_t phase = tick % period;
    if (phase < 0)
        phase += period;
    return phase >= period - rhythm->on;
}

int ta_player_swap_rate(const ta_player *player, int layer_index, int x, int y, int w, int h)
{
    const ta_map *map = player->map;
    const ta_layer *layer;
    int a, t, rate = 0;
    if (layer_index < 0 || layer_index >= map->layer_count)
        return 0;
    layer = &map->layers[layer_index];
    for (a = 0; a < layer->animation_count; a++) {
        const ta_animation *anim = &layer->animations[a];
        int32_t now = 0;
        if (!animation_live(player, layer_index, a, &now))
            continue;
        for (t = 0; t < anim->track_count; t++) {
            const ta_track *track = &anim->tracks[t];
            const int tw = track->src_w > 0 ? track->src_w : layer->width;
            const int th = track->src_h > 0 ? track->src_h : layer->height;
            ta_pose pose;
            if (!swap_usable(map, layer_index, track))
                continue;
            if (x >= track->src_x + tw || y >= track->src_y + th || x + w <= track->src_x || y + h <= track->src_y)
                continue;
            eval_track(track, now, &pose);
            rate += pose.rate;
        }
    }
    return rate;
}

/* The swap track that speaks for a cell: the last one playing whose region
 * holds it, or failing that (when `any`) the last one at all. */
static const ta_track *swap_track_at(const ta_player *player, int layer_index, int cx, int cy, int any, int32_t *now_out)
{
    const ta_map *map = player->map;
    const ta_layer *layer = &map->layers[layer_index];
    const ta_track *playing = NULL, *other = NULL;
    int a, t;
    for (a = 0; a < layer->animation_count; a++) {
        const ta_animation *anim = &layer->animations[a];
        int32_t now = 0;
        const int live = animation_live(player, layer_index, a, &now);
        for (t = 0; t < anim->track_count; t++) {
            const ta_track *track = &anim->tracks[t];
            if (!swap_usable(map, layer_index, track) || !region_holds(track, layer, cx, cy))
                continue;
            if (live) {
                playing = track;
                if (now_out)
                    *now_out = now;
            } else {
                other = track;
            }
        }
    }
    return playing ? playing : (any ? other : NULL);
}

int ta_player_swap_rhythm(const ta_player *player, int layer, int cx, int cy, ta_swap_rhythm *out)
{
    const ta_track *track;
    ta_pose pose;
    int32_t now = 0;
    if (layer < 0 || layer >= player->map->layer_count)
        return 0;
    track = swap_track_at(player, layer, cx, cy, 0, &now);
    if (!track)
        return 0;
    eval_track(track, now, &pose);
    out->rate = pose.rate;
    out->period = pose.period;
    out->on = pose.on;
    return 1;
}

int ta_player_swap_state(const ta_player *player, int layer_index, int cx, int cy)
{
    const ta_map *map = player->map;
    const ta_layer *layer;
    const ta_track *track;
    if (layer_index < 0 || layer_index >= map->layer_count || player->in->swap_offset[layer_index] < 0)
        return -1;
    layer = &map->layers[layer_index];
    if (cell_of(layer, cx, cy) == 0)
        return -1;
    track = swap_track_at(player, layer_index, cx, cy, 1, NULL);
    if (!track || cell_of(&map->layers[track->source], cx, cy) == 0)
        return -1;
    return player->swapped[(size_t)player->in->swap_offset[layer_index] + (size_t)cy * (size_t)layer->width + (size_t)cx] != 0;
}

int ta_player_swap_set(ta_player *player, int layer_index, int cx, int cy, int in)
{
    const ta_map *map = player->map;
    const ta_layer *layer;
    const ta_track *track;
    uint8_t *cell;
    if (layer_index < 0 || layer_index >= map->layer_count || player->in->swap_offset[layer_index] < 0)
        return -1;
    layer = &map->layers[layer_index];
    if (cell_of(layer, cx, cy) == 0)
        return -1;
    track = swap_track_at(player, layer_index, cx, cy, 1, NULL);
    if (!track || cell_of(&map->layers[track->source], cx, cy) == 0)
        return -1;
    cell = &player->swapped[(size_t)player->in->swap_offset[layer_index] + (size_t)cy * (size_t)layer->width + (size_t)cx];
    if ((*cell != 0) == (in != 0))
        return 0;
    *cell = in ? (uint8_t)(track->source + 1) : 0;
    return 1;
}

void ta_player_swap_restore(ta_player *player, int layer)
{
    if (layer < 0 || layer >= player->map->layer_count || player->in->swap_offset[layer] < 0)
        return;
    memset(player->swapped + player->in->swap_offset[layer], 0,
           (size_t)player->map->width * (size_t)player->map->height);
}

/* ================================================================= drawing */

typedef struct ta_clip {
    int x0, y0, x1, y1; /* map pixels, x1 and y1 exclusive */
} ta_clip;

/* One tile onto the target: `put` is handed the tile's rectangle in its image
 * and where it goes, and does the pixels. */
typedef void (*ta_put)(void *context, int tileset, int sx, int sy, int tw, int th, uint32_t flags, int dx, int dy,
                       int alpha, const ta_clip *clip);

/* Cells (cx0, cy0, cw x ch) of a layer, the first of them at map pixel
 * (px, py). `swaps_of` >= 0 draws that layer's swapped cells in. */
static void draw_cells(const ta_player *player, const ta_layer *source, int swaps_of, int cx0, int cy0, int cw, int ch,
                       int px, int py, int alpha, const ta_clip *clip, ta_put put, void *context)
{
    const ta_map *map = player->map;
    const int tw = map->tile_width, th = map->tile_height;
    int row, col, first_row = 0, last_row = ch, first_col = 0, last_col = cw;

    if (alpha <= 0 || !source->data || clip->x1 <= clip->x0 || clip->y1 <= clip->y0)
        return;
    /* Only the cells the clip can show. A tile taller or wider than the grid
     * reaches up and right of its cell, so look one further each way. */
    if (clip->y0 > py) first_row = (clip->y0 - py) / th - 1;
    if (clip->x0 > px) first_col = (clip->x0 - px) / tw - 1;
    if (clip->y1 < py + ch * th) last_row = (clip->y1 - py) / th + 2;
    if (clip->x1 < px + cw * tw) last_col = (clip->x1 - px) / tw + 2;
    if (first_row < 0) first_row = 0;
    if (first_col < 0) first_col = 0;
    if (last_row > ch) last_row = ch;
    if (last_col > cw) last_col = cw;

    for (row = first_row; row < last_row; row++) {
        for (col = first_col; col < last_col; col++) {
            const int cx = cx0 + col, cy = cy0 + row;
            const uint32_t cell = swaps_of >= 0 ? ta_player_gid(player, swaps_of, cx, cy) : cell_of(source, cx, cy);
            const ta_tileset *tileset;
            int index, sx, sy;
            if ((cell & TA_GID_MASK) == 0)
                continue;
            index = ta_map_tileset_of(map, cell);
            if (index < 0)
                continue;
            tileset = &map->tilesets[index];
            if ((cell & TA_GID_MASK) - tileset->first_gid >= (uint32_t)tileset->tile_count)
                continue;
            ta_tileset_tile(tileset, cell, &sx, &sy);
            /* Tiles larger than the grid stand on the bottom of their cell, as in Tiled. */
            put(context, index, sx, sy, tileset->tile_width, tileset->tile_height, cell & ~TA_GID_MASK,
                px + col * tw, py + row * th + th - tileset->tile_height, alpha, clip);
        }
    }
}

static int positive_mod(int value, int by)
{
    int result = value % by;
    return result < 0 ? result + by : result;
}

static void clip_to(ta_clip *clip, int x0, int y0, int x1, int y1)
{
    if (clip->x0 < x0) clip->x0 = x0;
    if (clip->y0 < y0) clip->y0 = y0;
    if (clip->x1 > x1) clip->x1 = x1;
    if (clip->y1 > y1) clip->y1 = y1;
}

static void draw_blit(const ta_player *player, const ta_layer_pose *pose, const ta_blit *blit, ta_clip clip,
                      ta_put put, void *context)
{
    const ta_layer *source = &player->map->layers[blit->source_layer];
    if (source->type != TA_LAYER_TILE)
        return;
    /* Drawn only inside its window, which stays put on the map. */
    if (blit->mask_w > 0 && blit->mask_h > 0)
        clip_to(&clip, blit->mask_x, blit->mask_y, blit->mask_x + blit->mask_w, blit->mask_y + blit->mask_h);
    draw_cells(player, source, -1, blit->src_x, blit->src_y, blit->w, blit->h, blit->x + pose->offset_x,
               blit->y + pose->offset_y, pose->alpha * blit->alpha / 255, &clip, put, context);
}

#define TA_MAX_BLITS 64

static void draw_layer(const ta_player *player, int layer_index, ta_clip view, int what, ta_put put, void *context)
{
    const ta_map *map = player->map;
    const ta_layer *layer;
    ta_layer_pose pose;
    ta_blit blits[TA_MAX_BLITS];
    int count, i;

    if (layer_index < 0 || layer_index >= map->layer_count || !ta_player_layer_visible(player, layer_index))
        return;
    layer = &map->layers[layer_index];
    if (layer->type != TA_LAYER_TILE)
        return;
    ta_player_layer_pose(player, layer_index, &pose);
    if (what & TA_DRAW_OPAQUE)
        pose.alpha = 255;

    if (what & TA_DRAW_TILES) {
        if (pose.wrap) {
            /* A scrolling layer repeats: it and its neighbours, clipped to the map. */
            const int width = map->width * map->tile_width, height = map->height * map->tile_height;
            const int ox = positive_mod(pose.offset_x, width), oy = positive_mod(pose.offset_y, height);
            ta_clip clip = view;
            int copy_x, copy_y;
            clip_to(&clip, 0, 0, width, height);
            for (copy_y = 0; copy_y < 2; copy_y++)
                for (copy_x = 0; copy_x < 2; copy_x++)
                    draw_cells(player, layer, layer_index, 0, 0, layer->width, layer->height, ox - copy_x * width,
                               oy - copy_y * height, pose.alpha, &clip, put, context);
        } else {
            draw_cells(player, layer, layer_index, 0, 0, layer->width, layer->height, pose.offset_x, pose.offset_y,
                       pose.alpha, &view, put, context);
        }
    }
    if (what & TA_DRAW_BLITS) {
        count = ta_player_blits(player, layer_index, blits, TA_MAX_BLITS);
        if (count > TA_MAX_BLITS)
            count = TA_MAX_BLITS;
        for (i = 0; i < count; i++) {
            if (what & TA_DRAW_OPAQUE)
                blits[i].alpha = 255;
            draw_blit(player, &pose, &blits[i], view, put, context);
        }
    }
}

/* Where a target pixel of a tile comes from in its image, flips undone. */
static void unflip(uint32_t flags, int tw, int th, int *u, int *v)
{
    if (flags & TA_FLIP_H) *u = tw - 1 - *u;
    if (flags & TA_FLIP_V) *v = th - 1 - *v;
    if (flags & TA_FLIP_D) {
        const int swap = *u;
        *u = *v;
        *v = swap;
    }
}

/* --- eight bits --- */

typedef struct ta_context8 {
    const ta_image8 *images;
    const ta_target8 *target;
} ta_context8;

static void put8(void *context, int tileset, int sx, int sy, int tw, int th, uint32_t flags, int dx, int dy,
                 int alpha, const ta_clip *clip)
{
    const ta_context8 *c = (const ta_context8 *)context;
    const ta_image8 *image = &c->images[tileset];
    const ta_target8 *target = c->target;
    int x0 = dx, y0 = dy, x1 = dx + tw, y1 = dy + th, x, y;

    /* Eight bits cannot blend. What is partly transparent is drawn through an
     * ordered dither that stays put on the screen: `level` pixels in 16. */
    static const uint8_t bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    const int level = alpha >= 255 ? 16 : (alpha * 16 + 127) / 255;

    if (!image->pixels || level <= 0)
        return;
    if (x0 < clip->x0) x0 = clip->x0;
    if (y0 < clip->y0) y0 = clip->y0;
    if (x1 > clip->x1) x1 = clip->x1;
    if (y1 > clip->y1) y1 = clip->y1;
    for (y = y0; y < y1; y++) {
        const int screen_y = y - target->origin_y;
        uint8_t *row = target->pixels + (size_t)screen_y * (size_t)target->stride;
        for (x = x0; x < x1; x++) {
            int u = x - dx, v = y - dy;
            const int screen_x = x - target->origin_x;
            uint8_t value;
            if (level < 16 && bayer[(screen_y & 3) * 4 + (screen_x & 3)] >= level)
                continue;
            if (flags)
                unflip(flags, tw, th, &u, &v);
            if (sx + u >= image->width || sy + v >= image->height)
                continue;
            value = image->pixels[(size_t)(sy + v) * (size_t)image->stride + (size_t)(sx + u)];
            if (value != image->transparent)
                row[screen_x] = value;
        }
    }
}

static ta_clip view8(const ta_target8 *target)
{
    ta_clip clip;
    clip.x0 = target->origin_x;
    clip.y0 = target->origin_y;
    clip.x1 = target->origin_x + target->width;
    clip.y1 = target->origin_y + target->height;
    return clip;
}

void ta_draw_layer8(const ta_player *player, int layer, const ta_image8 *images, const ta_target8 *target, int what)
{
    ta_context8 context;
    context.images = images;
    context.target = target;
    draw_layer(player, layer, view8(target), what, put8, &context);
}

void ta_draw_blit8(const ta_player *player, int layer, const ta_blit *blit, const ta_image8 *images,
                   const ta_target8 *target)
{
    ta_context8 context;
    ta_layer_pose pose;
    context.images = images;
    context.target = target;
    ta_player_layer_pose(player, layer, &pose);
    draw_blit(player, &pose, blit, view8(target), put8, &context);
}

/* --- true color --- */

typedef struct ta_context32 {
    const ta_image32 *images;
    const ta_target32 *target;
} ta_context32;

static void put32(void *context, int tileset, int sx, int sy, int tw, int th, uint32_t flags, int dx, int dy,
                  int alpha, const ta_clip *clip)
{
    const ta_context32 *c = (const ta_context32 *)context;
    const ta_image32 *image = &c->images[tileset];
    const ta_target32 *target = c->target;
    int x0 = dx, y0 = dy, x1 = dx + tw, y1 = dy + th, x, y, k;

    if (!image->pixels || alpha <= 0)
        return;
    if (x0 < clip->x0) x0 = clip->x0;
    if (y0 < clip->y0) y0 = clip->y0;
    if (x1 > clip->x1) x1 = clip->x1;
    if (y1 > clip->y1) y1 = clip->y1;
    for (y = y0; y < y1; y++) {
        uint8_t *row = target->pixels + (size_t)(y - target->origin_y) * (size_t)target->stride;
        for (x = x0; x < x1; x++) {
            int u = x - dx, v = y - dy;
            const uint8_t *from;
            uint8_t *to = row + (size_t)(x - target->origin_x) * 4u;
            int a;
            if (flags)
                unflip(flags, tw, th, &u, &v);
            if (sx + u >= image->width || sy + v >= image->height)
                continue;
            from = image->pixels + (size_t)(sy + v) * (size_t)image->stride + (size_t)(sx + u) * 4u;
            a = from[3] * alpha / 255;
            if (a <= 0)
                continue;
            if (a >= 255) {
                to[0] = from[0];
                to[1] = from[1];
                to[2] = from[2];
                to[3] = 255;
                continue;
            }
            for (k = 0; k < 3; k++)
                to[k] = (uint8_t)((from[k] * a + to[k] * (255 - a) + 127) / 255);
            to[3] = (uint8_t)(a + to[3] * (255 - a) / 255);
        }
    }
}

static ta_clip view32(const ta_target32 *target)
{
    ta_clip clip;
    clip.x0 = target->origin_x;
    clip.y0 = target->origin_y;
    clip.x1 = target->origin_x + target->width;
    clip.y1 = target->origin_y + target->height;
    return clip;
}

void ta_draw_layer32(const ta_player *player, int layer, const ta_image32 *images, const ta_target32 *target, int what)
{
    ta_context32 context;
    context.images = images;
    context.target = target;
    draw_layer(player, layer, view32(target), what, put32, &context);
}

void ta_draw_blit32(const ta_player *player, int layer, const ta_blit *blit, const ta_image32 *images,
                    const ta_target32 *target)
{
    ta_context32 context;
    ta_layer_pose pose;
    context.images = images;
    context.target = target;
    ta_player_layer_pose(player, layer, &pose);
    draw_blit(player, &pose, blit, view32(target), put32, &context);
}
