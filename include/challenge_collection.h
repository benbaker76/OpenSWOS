#ifndef CHALLENGE_COLLECTION_H
#define CHALLENGE_COLLECTION_H

#include "swos_types.h"

#define COLLECTION_COUNT 6
enum { COLLECTION_ABORTED, COLLECTION_LOST, COLLECTION_WON };

/* Stable IDs are never reused when a starting position/objective changes. */
typedef struct {
    uint32_t id;
    const char *title;
    const char *teams;
    const char *human;
    const char *start;
    const char *restart;
    const char *asset;
    int stars;              /* difficulty 1..5, drawn as the squad list's value stars */
    const char *story[2];   /* two lines under the list while the row is highlighted */
} CollectionChallenge;
extern const CollectionChallenge g_challengeCollection[COLLECTION_COUNT];

void ChallengeCollection_Show(void);
void ChallengeCollection_MenuInit(void);
void ChallengeCollection_MenuSelect(void *entry);

void ChallengeProgress_Load(void);
int ChallengeProgress_Save(void);
int ChallengeProgress_Done(int index);
int ChallengeProgress_Count(void);
int ChallengeProgress_Dirty(void);
int ChallengeProgress_Recovered(void);
void ChallengeProgress_Begin(int index, int humanSide);
void ChallengeProgress_Restart(void);
void ChallengeProgress_PositionLoaded(void);
/* winner: 1 or 2 (team 1/2, after extra time or penalties), 0 for a draw */
void ChallengeProgress_FinalWhistle(unsigned goals1, unsigned goals2, int winner);
void ChallengeProgress_End(void);
int ChallengeProgress_InAttempt(void);   /* a collection challenge is being played */
int ChallengeProgress_Result(void);
unsigned ChallengeProgress_Goals(int side);

#endif
