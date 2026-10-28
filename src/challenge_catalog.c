#include "challenge_collection.h"

#ifdef SWOS_SDL_BUILD
#define ASSET(n) "data/CCS0" n ".DAT"
#else
#define ASSET(n) "data/CCA0" n ".DAT"
#endif

const CollectionChallenge g_challengeCollection[COLLECTION_COUNT] = {
    { 1, "FINISH THE JOB", "AC MILAN V JUVENTUS", "AC MILAN",
      "75 MINUTES   2-0", "JUVENTUS KICK OFF", ASSET("1") },
    { 2, "HOLD YOUR NERVE", "ITALY V MALTA", "ITALY",
      "80 MINUTES   1-0", "CORNER FOR MALTA", ASSET("2") },
    { 3, "FIND THE WINNER", "LIVERPOOL V EVERTON", "LIVERPOOL",
      "75 MINUTES   0-0", "CORNER FOR LIVERPOOL", ASSET("3") },
    { 4, "TURN IT AROUND", "BARCELONA V REAL MADRID", "BARCELONA",
      "65 MINUTES   0-1", "BARCELONA KICK OFF", ASSET("4") },
    { 5, "AGAINST THE CLOCK", "JUVENTUS V AC MILAN", "JUVENTUS",
      "80 MINUTES   0-2", "JUVENTUS KICK OFF", ASSET("5") },
    { 6, "DERBY MIRACLE", "PARTIZAN V RED STAR", "PARTIZAN",
      "67 MINUTES   0-3", "CORNER FOR PARTIZAN", ASSET("6") }
};
