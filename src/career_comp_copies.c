/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* career_comp_copies.c — UNTANGLE step 3 (overlay retirement, category C).
 *
 * The 34 per-career *Copy competition records (euroCupCopy ..
 * oceaniaWorldCupQualificationCopy) lifted out of the careerFileBuffer overlay
 * into standalone storage. Each is the runtime working copy of a static
 * competition template (euroCup, cupWinnersCup, ...); career_init.c:InitCareer
 * builds them by iterating the static `careerContests[]` table
 * (competition_data.c) and memcpy-ing template -> *Copy, and the JSON save
 * RECONSTRUCTS them the same way on load (reconstruct_comp_copy_region_from_-
 * templates) rather than byte-loading them — only two dynamic bytes (the
 * worldCupQualifyingGroup8Copy / oceaniaWorldCupQualificationCopy team counts at
 * offset 0xD) are carried in the save.
 *
 * Why this is format-neutral: `careerContests[].competitionCopy` is initialised
 * with the ADDRESS of each *Copy symbol (`(CompetitionCup *)&euroCupCopy`, ...),
 * so both the InitCareer fill loop and the save reconstruction write through
 * &symbol — relocating the storage here makes those initialisers resolve to the
 * new addresses automatically. Because the records are reconstructed (not
 * byte-loaded), the overlay hole left behind stays zero in both the live image
 * and the reload, so SWOS_SAVE_FORMAT_JSON_VERIFY remains 0 by construction.
 *
 * Each record carries a `text[2]` pointer pair, so its size grows by 8 bytes on
 * 64-bit (32-bit pointers -> 64-bit). The per-record reservation therefore keeps
 * the same conditional +8 pad the overlay used; the typed externs in
 * competition_data.c (CompetitionTournament euroCupCopy, etc.) resolve to these
 * raw-byte labels, exactly as they did when the storage lived in the overlay.
 * Emitted via __asm__ real labels (matching the overlay's own style) so no
 * competition-type header include is needed here (see
 * project_career_season_includes for the include-conflict hazard).
 *
 * Region size: 1832 bytes (32-bit) + 34*8 = 2104 bytes (64-bit). The overlay run
 * is replaced by the matching conditional .skip hole (no cascade).
 */
#include "swos_types.h"
#include "swos_platform.h"

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — see docs/WASM.md §4b). Simple split: the typed base was unused and
 * there is no cross-copy arithmetic; each *Copy is its own buffer, addressed via
 * &symbol by careerContests[].competitionCopy and reconstructed on load, so the
 * relocation is save-neutral. CC64 reproduces the old conditional `.skip 8`
 * (the text[2] pointer pair grows 32->64-bit); aligned(4) preserves .p2align 2.
 * Kept raw-byte (no competition-type include) to avoid the include conflict. */
#define CC64 (UINTPTR_MAX != 0xFFFFFFFFu ? 8 : 0)

/* The three European cups: a world plays them as 36-club league phases (CompetitionLeaguePhase,
 * 120 bytes + CC64: src/league_phase.c), so with the worlds compiled in their copies hold one. A
 * copy's saved list runs to its template's size (the contest table's), so 96/97's saves are as
 * they were. */
#if SWOS_WORLDS
#define EURO_COPY(n) 120
#else
#define EURO_COPY(n) (n)
#endif
uint8_t _euroCupCopy                          [EURO_COPY(112) + CC64] __attribute__((aligned(4)));
uint8_t _cupWinnersCupCopy                    [EURO_COPY(92) + CC64] __attribute__((aligned(4)));
uint8_t _uefaCupCopy                          [EURO_COPY(92) + CC64] __attribute__((aligned(4)));
uint8_t _invitationTournamentCopy             [92 + CC64] __attribute__((aligned(4)));
uint8_t _tourCopy                             [92 + CC64] __attribute__((aligned(4)));
uint8_t _friendlyCopy                         [92 + CC64] __attribute__((aligned(4)));
uint8_t _europeanChampionships2Copy           [112 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup1Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup2Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup3Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup4Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup5Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup6Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup7Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _ecQualifyingGroup8Copy               [92 + CC64] __attribute__((aligned(4)));
uint8_t _copaAmericaCopy                      [112 + CC64] __attribute__((aligned(4)));
uint8_t _africanNationsCupCopy                [112 + CC64] __attribute__((aligned(4)));
uint8_t _northAmericanCupCopy                 [112 + CC64] __attribute__((aligned(4)));
uint8_t _asianCupCopy                         [112 + CC64] __attribute__((aligned(4)));
uint8_t _oceaniaCupCopy                       [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCup2Copy                        [112 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup1Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup2Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup3Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup4Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup5Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup6Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup7Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _worldCupQualifyingGroup8Copy         [92 + CC64] __attribute__((aligned(4)));
uint8_t _northAmericaWorldCupQualificationCopy[112 + CC64] __attribute__((aligned(4)));
uint8_t _southAmericaWorldCupQualificationCopy[112 + CC64] __attribute__((aligned(4)));
uint8_t _africaWorldCupQualificationCopy      [112 + CC64] __attribute__((aligned(4)));
uint8_t _asiaWorldCupQualificationCopy        [112 + CC64] __attribute__((aligned(4)));
uint8_t _oceaniaWorldCupQualificationCopy     [92 + CC64] __attribute__((aligned(4)));
