/*
 * <stdio.h> for the Amiga build (include/amiga_libc, -isystem in Makefile.amiga only).
 *
 * The m68k toolchain has no hosted libc. This is the part the shared code that also runs on the
 * Amiga (the mod API and the built-in mods) uses: formatting into a buffer, and the log lines --
 * fprintf to stderr/stdout lands in a ring buffer in memory (src/amiga_libc.c, _gAmigaLog), read
 * with the harness. There are no files here on purpose: a file goes through swos_io / resload,
 * so a mod's read sees the mod overlay and WHDLoad like every other read.
 */
#ifndef AMIGA_LIBC_STDIO_H
#define AMIGA_LIBC_STDIO_H

#include "swos_types.h"
#include <stdarg.h>

typedef struct AmigaFile FILE;
extern FILE *const stderr;
extern FILE *const stdout;
#define stderr stderr
#define stdout stdout

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int snprintf(char *buf, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int vfprintf(FILE *f, const char *fmt, va_list ap);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fputs(const char *s, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int putchar(int c);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int puts(const char *s);
int fflush(FILE *f);

#endif
