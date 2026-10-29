/*
 * save_format_json.h — JSON-based SWOS save file format.
 *
 * See SAVE_FORMAT_JSON.md for design rationale and schema.
 *
 * The JSON path is selected at compile time by `SWOS_SAVE_FORMAT_JSON`
 * (see Makefile). When ON, all SWOS_Save_* functions write JSON via
 * mjson. The load path always auto-detects: if the first non-whitespace
 * byte of the file is '{', JSON is parsed; otherwise the legacy
 * binary loader runs. This lets existing .CAR/.SEA/.PRE/.DIY/.HIL
 * files keep loading until the user re-saves and upgrades to JSON.
 *
 * Public API mirrors the legacy file_io.c entry points one-for-one so
 * the dispatcher in file_io.c is a simple "is it JSON?" branch.
 */
#ifndef SAVE_FORMAT_JSON_H
#define SAVE_FORMAT_JSON_H

#include "swos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the buffer's first non-whitespace byte is '{', else 0.
 * Used by file_io.c to dispatch JSON vs legacy after LoadFileEx
 * reads the file into the destination buffer. */
int  SaveJson_BufferIsJson(const uint8_t *buf, uint32_t _size);

/* Same check via stdio on `save/<filename>` — used when the file
 * size could exceed careerFileBuffer's capacity (JSON saves can be
 * several hundred KB). Returns 1 if JSON, 0 if not, -1 on open
 * error so the caller can fall back to the legacy load path. */
int  SaveJson_FileIsJson(const uint8_t *filename);

/* Per-file-type save/load. Each returns 0 on success, non-zero on
 * error (caller shows the appropriate error menu). */
int16_t SaveJson_WriteCareer(uint8_t *filename);
int16_t SaveJson_LoadCareer (uint8_t *filename, const uint8_t *src, uint32_t src_size);

int16_t SaveJson_WritePreset(uint8_t *filename);
int16_t SaveJson_LoadPreset (uint8_t *filename, const uint8_t *src, uint32_t src_size);

int16_t SaveJson_WriteDIY   (uint8_t *filename);
int16_t SaveJson_LoadDIY    (uint8_t *filename, const uint8_t *src, uint32_t src_size);
/* A .DIY's JSON text already in memory (NUL-terminated): a classic competition. */
int16_t SaveJson_LoadDIYData(const uint8_t *src, uint32_t src_size);

int16_t SaveJson_WriteHighlights(uint8_t *filename);
int16_t SaveJson_LoadHighlights (uint8_t *filename, const uint8_t *src, uint32_t src_size);

int16_t SaveJson_WriteTactics(uint8_t *filename, const uint8_t *src, uint32_t src_size);
int16_t SaveJson_LoadTactics (uint8_t *filename, uint8_t *dst, uint32_t dst_capacity);

/* Country/league byte from the last loaded career save's primary
 * competition. Read by Rebuild_CompetitionSlotPointers to recover
 * the competitionsTable[] index when selTeamNumber is 0xFFFF (season
 * mode with all-CPU teams). Returns 0xFF when no save has been
 * loaded or the save was from before this field existed. */
uint8_t SaveJson_GetLoadedPrimaryCountryCode(void);

/* ---- Data-file JSON loaders (bin/data_json) — docs/DATA_INTERNAL_JSON.md ----
 * Reuse the save-format struct machinery to load the team/competition JSON. */
struct TeamFileEx;
int DataJson_LoadTeamArray(const char *json, int len, struct TeamFileEx *out, int max);
#ifdef SWOS_SDL_BUILD
/* SWOS_DATA_JSON wire-in: load a country's teams from data_json/ into the
   teamFileBuf (2-byte host-native count + TeamFileEx records). Returns the team
   count (>0), or -1 to fall back to the binary TEAM.NNN. See LoadTeamFile. */
int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *teamFileBuf, int maxTeams);
/* Parse one cup/playoff competition record (JSON object slice) into a
   CompetitionCup (`out`, passed as void* — the type is an anonymous typedef).
   isPlayoff sets the Competition.isPlayoff discriminator; name[] strings are
   interned into strpool. Returns 0 on success. */
int DataJson_LoadCompetitionCup(const char *json, int len, void *out,
                                int isPlayoff, char *strpool, int strpoolsz);
/* Parse a type-2 group-tournament record (CompetitionTournament) / a type-0
   league record (CompetitionLeague) — same `out` is void* (anonymous typedef).
   League cross-ref playoffLinkPtrs[] are resolved later at the country-graph
   wire-in, not here. */
int DataJson_LoadCompetitionTournament(const char *json, int len, void *out,
                                       char *strpool, int strpoolsz);
int DataJson_LoadCompetitionLeague(const char *json, int len, void *out,
                                   char *strpool, int strpoolsz);
/* Parse a promotion-playoff record into a `Playoffs` struct (`out` is void* —
   anonymous typedef). DISTINCT from CompetitionCup: the Playoffs layout has
   roundRuleFlags[2] + explicit teams/awayGoalsMode/subsAndMaxSubs bytes and a
   runtime-filled qualifiedTeamIds[8], so the cup loader's layout does NOT fit.
   name[] strings are interned into strpool. Returns 0 on success. */
int DataJson_LoadPlayoffs(const char *json, int len, void *out,
                          char *strpool, int strpoolsz);
/* Runtime wire-in: rebuild the normal-country competition tables in
   competitionsTable[] from data_json/ at startup. Call once early in front-end
   setup. No-op unless SWOS_DATA_JSON is set; region/special tables and named
   competition globals stay compiled. */
void DataJson_InstallCompetitionGraph(void);
int DataJson_SelfTest(void);   /* run via SWOS_DATA_JSON_SELFTEST=1 */
#endif

#ifdef __cplusplus
}
#endif

#endif /* SAVE_FORMAT_JSON_H */
