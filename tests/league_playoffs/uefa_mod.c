/* The Mod's uefaq.c (included as it is) on the cases of uefa_cases.h. Built non-PIE: its eplay
 * keeps the play-offs' addresses in a u32, which holds them only below 4 GB. */
#include <stdio.h>
#include <stdlib.h>
#include UEFAQ_C
#define SETEP(rk, p) (UQ_IN.eplay[rk] = (u32)(unsigned long)(p))
#include "uefa_cases.h"
int main(int argc, char **argv) { return run(argc, argv); }
