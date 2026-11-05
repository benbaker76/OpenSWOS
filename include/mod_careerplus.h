/*
 * mod_careerplus.h -- Career+'s files (src/mod_careerplus*.c), to each other. Not an API.
 */
#ifndef MOD_CAREERPLUS_H
#define MOD_CAREERPLUS_H

#include <stdint.h>

#define CP_SLUG "careerplus"

/* the mod's random numbers (mod_careerplus.c): never the game's draws */
uint32_t CP_Rand(void);
int32_t  CP_RandBelow(int32_t n);       /* 0..n-1 */

struct ModCompetitionResult;
struct ModJsonWriter;

/* PAST WINNERS (mod_careerplus_winners.c) */
void CP_Winners_Init(void);
void CP_Winners_Clear(void);
void CP_Winners_Record(const struct ModCompetitionResult *result);
void CP_Winners_Save(struct ModJsonWriter *out);
void CP_Winners_Load(const char *json, int32_t len);   /* (json NULL: none) */

/* the player rows (mod_careerplus_rows.c) */
struct ModPlayerRow;
void CP_Rows_Drawn(const struct ModPlayerRow *row);

/* the trialists (mod_careerplus_trialists.c) */
struct ModPlayerCreated;
void CP_Trialists_Created(const struct ModPlayerCreated *created);

/* the clubs in memory (mod_careerplus_world.c) */
#include "game_types.h"
typedef struct {
    uint16_t   team;
    uint8_t    league;
    char       name[17];
    uint8_t    order[16];
    PlayerFile players[16];
} CP_Club;
int32_t  CP_World_Read(void);               /* every club now (ModApi_CareerClubs); -1 not */
int32_t  CP_World_Count(void);
CP_Club *CP_World_Club(int32_t i);          /* (a file's clubs together) */
CP_Club *CP_World_Find(uint16_t team);
int32_t  CP_World_Write(const CP_Club *club, int32_t slot);     /* its player back to the career */
int32_t  CP_Numbers(const char *s, int32_t len, int32_t *out, int32_t max);   /* a slice's integers */
int32_t  CP_PlayerValid(const PlayerFile *p);   /* a player with a value */
uint8_t  CP_Position(const PlayerFile *p);      /* 0 GK .. 7 attacker */
void     CP_Ratings(const PlayerFile *p, uint8_t a[7]);
void     CP_SetRatings(PlayerFile *p, const uint8_t a[7]);

/* the ages and retirements (mod_careerplus_ages.c) */
struct ModPlayerMove;
int32_t  CP_Age(uint16_t team, int32_t slot);
void     CP_SetAge(uint16_t team, int32_t slot, int32_t age);
void     CP_SwapAges(uint16_t teamA, int32_t slotA, uint16_t teamB, int32_t slotB);
uint16_t CP_MovedBatch(uint16_t team, int32_t slot);
void     CP_SetMovedBatch(uint16_t team, int32_t slot, uint16_t batch);
void     CP_Ages_New(void);
void     CP_Ages_Save(struct ModJsonWriter *out);
void     CP_Ages_Load(const char *json, int32_t len);
void     CP_Ages_Moved(const struct ModPlayerMove *move);
void     CP_Ages_Created(const struct ModPlayerCreated *created);
void     CP_Ages_SeasonEnd(void);           /* everyone a year older */
void     CP_Ages_Retire(uint16_t season);   /* the CP_World clubs read already */
int32_t  CP_News_Count(void);
void     CP_News_Clear(void);
int32_t  CP_News_Season(void);
const char *CP_News_Line(int32_t i, uint8_t *age, uint8_t *position);

/* the AI transfers (mod_careerplus_ai.c) */
void     CP_AI_Init(void);
int32_t  CP_AI_Batch(void);                 /* the CP_World clubs read already */
uint16_t CP_AI_Batches(void);
void     CP_AI_SetBatches(uint16_t n);

/* the player searches (mod_careerplus_search.c) */
void CP_Search_Init(void);
void CP_Search_Stale(void);                 /* the career's squads changed: read them again */
void CP_Search_Fresh(void);                 /* CP_World has the career's squads as they are */

#endif
