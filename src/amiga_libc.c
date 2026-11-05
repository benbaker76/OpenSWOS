/*
 * amiga_libc.c -- the C library the Amiga build lacks, for the shared code that runs there too
 * (include/amiga_libc: the headers; -isystem in Makefile.amiga only). SDL builds use the host's.
 *
 * What is here is what the mod API and the built-in mods use (docs/MODS.md §30): formatting into
 * a buffer (no floating point: none of that code prints one), the heap over swos_io_alloc, the
 * string calls, strtol, qsort. strlen/strcmp/strncmp/memcmp/atoi stay in src/swos_io.c and
 * memcpy/memset/memmove in src/main.c, where they always were.
 *
 * fprintf/printf/puts go to a ring buffer in memory, _gAmigaLog (the newest text before
 * _gAmigaLogPos), so the Amiga's log lines are a memory read in the harness rather than lost.
 */
#if defined(__m68k__) || defined(__amigaos__) || defined(__amigaos4__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swos_io.h"

/* ---- formatting ---- */

typedef struct { char *buf; size_t cap; size_t len; } Out;

static void out_ch(Out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void out_pad(Out *o, char c, int32_t n)
{
    while (n-- > 0)
        out_ch(o, c);
}

/* v / 10 and v % 10 for a 64-bit v with 32-bit divides (no libgcc in this link) */
static uint32_t div10_u64(uint64_t *v)
{
    uint32_t hi = (uint32_t)(*v >> 32), lo = (uint32_t)*v;
    uint32_t qhi = hi / 10, r = hi % 10;
    uint32_t mid = (r << 16) | (lo >> 16);
    uint32_t qmid = mid / 10;
    r = mid % 10;
    uint32_t low = (r << 16) | (lo & 0xFFFF);
    uint32_t qlo = low / 10;
    r = low % 10;
    *v = ((uint64_t)qhi << 32) | (qmid << 16) | qlo;
    return r;
}

static int32_t fmt_u64(char *tmp, uint64_t v, uint32_t base, int32_t upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int32_t n = 0;
    do {
        uint32_t d;
        if (base == 10) d = div10_u64(&v);
        else if (base == 16) { d = (uint32_t)v & 15; v >>= 4; }   /* (constant shifts: no libgcc) */
        else { d = (uint32_t)v & 7; v >>= 3; }
        tmp[n++] = digits[d];
    } while (v);
    return n;   /* reversed */
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    Out o = { buf, cap, 0 };
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { out_ch(&o, *p); continue; }
        p++;
        int32_t left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        for (;; p++) {
            if (*p == '-') left = 1;
            else if (*p == '0') zero = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '#') alt = 1;
            else break;
        }
        int32_t width = 0, prec = -1;
        if (*p == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } p++; }
        else while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') { prec = va_arg(ap, int); p++; }
            else while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        int32_t lng = 0;    /* 0 int, 1 long, 2 long long */
        for (;; p++) {
            if (*p == 'h') continue;
            if (*p == 'l') { lng++; continue; }
            if (*p == 'z' || *p == 't' || *p == 'j') { lng = (*p == 'j') ? 2 : 1; continue; }
            break;
        }
        char c = *p;
        if (!c) break;
        if (c == '%') { out_ch(&o, '%'); continue; }
        if (c == 'c') {
            if (!left) out_pad(&o, ' ', width - 1);
            out_ch(&o, (char)va_arg(ap, int));
            if (left) out_pad(&o, ' ', width - 1);
            continue;
        }
        if (c == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int32_t n = 0;
            while (s[n] && (prec < 0 || n < prec)) n++;
            if (!left) out_pad(&o, ' ', width - n);
            for (int32_t i = 0; i < n; i++) out_ch(&o, s[i]);
            if (left) out_pad(&o, ' ', width - n);
            continue;
        }
        uint64_t u = 0;
        int32_t neg = 0, base = 10, upper = 0;
        if (c == 'd' || c == 'i') {
            int64_t v = lng == 2 ? va_arg(ap, long long) : lng == 1 ? (int64_t)va_arg(ap, long) : (int64_t)va_arg(ap, int);
            if (v < 0) { neg = 1; u = (uint64_t)-v; } else u = (uint64_t)v;
        } else if (c == 'u' || c == 'x' || c == 'X' || c == 'o' || c == 'p') {
            if (c == 'p') { u = (uintptr_t)va_arg(ap, void *); base = 16; alt = 1; }
            else u = lng == 2 ? va_arg(ap, unsigned long long) : lng == 1 ? (uint64_t)va_arg(ap, unsigned long)
                                                                         : (uint64_t)va_arg(ap, unsigned int);
            if (c == 'x' || c == 'X') base = 16;
            if (c == 'o') base = 8;
            upper = c == 'X';
        } else {    /* (floating point and the rest: not here) */
            out_ch(&o, '?');
            continue;
        }
        char tmp[24];
        int32_t n = (prec == 0 && u == 0) ? 0 : fmt_u64(tmp, u, (uint32_t)base, upper);
        int32_t digits = prec > n ? prec : n;
        const char *pre = neg ? "-" : plus ? "+" : space ? " " : "";
        int32_t prel = (int32_t)strlen(pre);
        if (alt && base == 16 && u) prel += 2;
        int32_t total = prel + digits;
        if (prec >= 0) zero = 0;
        if (!left && !zero) out_pad(&o, ' ', width - total);
        for (const char *q = pre; *q; q++) out_ch(&o, *q);
        if (alt && base == 16 && u) { out_ch(&o, '0'); out_ch(&o, upper ? 'X' : 'x'); }
        if (!left && zero) out_pad(&o, '0', width - total);
        out_pad(&o, '0', digits - n);
        while (n) out_ch(&o, tmp[--n]);
        if (left) out_pad(&o, ' ', width - total);
    }
    if (cap)
        buf[o.len < cap ? o.len : cap - 1] = '\0';
    return (int)o.len;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- the log: a ring in memory ---- */

struct AmigaFile { int32_t unused; };
static struct AmigaFile s_logFile;
FILE *const stderr = &s_logFile;
FILE *const stdout = &s_logFile;

#define AMIGA_LOG_SIZE 4096
/* (in .data, not .bss: the harness's gdb relocates every section by the code's offset, which is
 *  right for the data hunk and wrong for the BSS, so a BSS ring could not be read there) */
__attribute__((used, section(".data"))) char _gAmigaLog[AMIGA_LOG_SIZE];
__attribute__((used, section(".data"))) uint32_t _gAmigaLogPos;   /* (total written; next byte is % size) */

int fputs(const char *s, FILE *f)
{
    (void)f;
    for (; *s; s++)
        _gAmigaLog[_gAmigaLogPos++ % AMIGA_LOG_SIZE] = *s;
    return 0;
}

int puts(const char *s) { fputs(s, stdout); return fputs("\n", stdout); }
/* (GCC turns a printf with nothing to format into these) */
int fputc(int c, FILE *f) { (void)f; _gAmigaLog[_gAmigaLogPos++ % AMIGA_LOG_SIZE] = (char)c; return c; }
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    const char *s = (const char *)p;
    for (size_t i = 0; i < size * n; i++)
        fputc(s[i], f);
    return n;
}
int fflush(FILE *f) { (void)f; return 0; }

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char line[256];
    int n = vsnprintf(line, sizeof line, fmt, ap);
    fputs(line, f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- the heap ---- */

void *malloc(size_t n) { return swos_io_alloc((uint32_t)n); }
void  free(void *p) { swos_io_free(p); }
void *realloc(void *p, size_t n) { return swos_io_realloc(p, (uint32_t)n); }
void *calloc(size_t n, size_t size)
{
    uint32_t total = (uint32_t)(n * size);
    void *p = swos_io_alloc(total);
    if (p) memset(p, 0, total);
    return p;
}

char *getenv(const char *name) { (void)name; return NULL; }

/* ---- numbers ---- */

int  abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

unsigned long strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0) base = p[0] == '0' ? 8 : 10;
    unsigned long v = 0;
    const char *start = p;
    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (end) *end = (char *)(p == start ? s : p);
    return neg ? (unsigned long)-(long)v : v;
}

long strtol(const char *s, char **end, int base) { return (long)strtoul(s, end, base); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* (a shell sort: small, no recursion, and the lists sorted here are a few dozen) */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    uint8_t *a = (uint8_t *)base;
    for (size_t gap = n / 2; gap > 0; gap /= 2)
        for (size_t i = gap; i < n; i++)
            for (size_t j = i; j >= gap && cmp(a + (j - gap) * size, a + j * size) > 0; j -= gap) {
                uint8_t *x = a + (j - gap) * size, *y = a + j * size;
                for (size_t k = 0; k < size; k++) { uint8_t t = x[k]; x[k] = y[k]; y[k] = t; }
            }
}

/* ---- strings ---- */

void *memchr(const void *s, int c, size_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    for (; n--; p++)
        if (*p == (uint8_t)c) return (void *)p;
    return NULL;
}

char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++) != '\0') {} return r; }
char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = '\0';
    return d;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n)
{
    char *p = d + strlen(d);
    while (n-- && *s) *p++ = *s++;
    *p = '\0';
    return d;
}
char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}
char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char *)r;
    }
}
char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, nl) == 0) return (char *)h;
    return NULL;
}
size_t strcspn(const char *s, const char *reject)
{
    size_t i = 0;
    for (; s[i] && !strchr(reject, s[i]); i++) {}
    return i;
}
size_t strspn(const char *s, const char *accept)
{
    size_t i = 0;
    for (; s[i] && strchr(accept, s[i]); i++) {}
    return i;
}
char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

#endif /* the Amiga */
