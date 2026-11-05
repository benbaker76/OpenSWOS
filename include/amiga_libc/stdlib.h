/*
 * <stdlib.h> for the Amiga build (include/amiga_libc; see stdio.h there). The heap is
 * swos_io_alloc's (AllocMemAny: Exec, or WHDLoad's ExpMem heap). getenv answers NULL: the
 * Amiga has no environment for the game.
 */
#ifndef AMIGA_LIBC_STDLIB_H
#define AMIGA_LIBC_STDLIB_H

#include "swos_types.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void  free(void *p);

int   atoi(const char *s);
long  atol(const char *s);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int   abs(int v);
long  labs(long v);
void  qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
char *getenv(const char *name);

#endif
