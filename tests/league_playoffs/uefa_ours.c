/* src/uefa_access.c on the cases of uefa_cases.h. */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/uefa_access.c"
#define SETEP(rk, p) (UQ_IN.eplay[rk] = (p))
#include "uefa_cases.h"
int main(int argc, char **argv) { return run(argc, argv); }
