/*
 * <string.h> for the Amiga build (include/amiga_libc; see stdio.h there). memcpy/memset/memmove
 * are src/main.c's, strlen/strcmp/strncmp/memcmp src/swos_io.c's, the rest src/amiga_libc.c's.
 */
#ifndef AMIGA_LIBC_STRING_H
#define AMIGA_LIBC_STRING_H
#define _STRING_H_          /* (the toolchain's own, sys-include/string.h: these replace it) */

#include "swos_types.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

void  *memcpy(void *d, const void *s, unsigned long n);
void  *memmove(void *d, const void *s, unsigned long n);
void  *memset(void *d, int c, unsigned long n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strcat(char *d, const char *s);
char  *strncat(char *d, const char *s, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char  *strdup(const char *s);

#endif
