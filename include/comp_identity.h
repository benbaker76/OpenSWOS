/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* comp_identity.h — competition identity by compId.
 *
 * Part of the competition dynamic / pointer-free refactor (see
 * docs/COMPETITION_DYNAMIC_REFACTOR.md). It replaces by-ADDRESS competition
 * identity gates — `comp == &_xCopy`, `comp >= &_aCopy && comp <= &_bCopy` —
 * with by-ID integer compares: `comp_id(c) == COMPID_x`.
 *
 * Why compId is the right runtime id:
 *   - It is byte 0 of every Competition record (the struct head shared by
 *     CompetitionTournament / CompetitionCup / Playoffs / CompetitionLeague),
 *     so reading it needs no competition-type header here (avoids the
 *     include-conflict hazard noted in project_career_season_includes).
 *   - A runtime working *Copy is memcpy'd from its static template
 *     (career_init.c:InitCareer / reconstruct_comp_copy_region_from_templates),
 *     so the *Copy carries the SAME compId as the template. One id compare
 *     therefore matches both template and copy — and removes the dependency on
 *     the compiled global's address and on the *Copy region's .skip layout.
 *   - It is already the loader's verify key (save_format_json.c sg[]/sp[]).
 *
 * The compId map below is extracted from the record literals in
 * competition_data.c (the `{ compId, type, ... }` struct head). The
 * internationals are deliberately dense (0..36), which makes the qualifier
 * RANGE checks contiguous — see comp_is_wc_qualifier / comp_is_ec_qualifier.
 *
 * The JSON string id (tournaments.json/cups.json/leagues.json "id") remains the
 * external/persistent identity; compId is the internal/runtime identity.
 */
#ifndef COMP_IDENTITY_H
#define COMP_IDENTITY_H

/* swos_types.h (NOT <stdint.h>): the Amiga m68k toolchain has no working
 * <stdint.h>, so the project defines uint8_t/etc. there and pulls in the libc
 * <stdint.h> only on the SDL build. */
#include "swos_types.h"

/* compId values, from the record heads in competition_data.c. */
enum CompId {
    COMPID_euroCup                           = 0,
    COMPID_cupWinnersCup                     = 1,
    COMPID_uefaCup                           = 2,
    COMPID_worldCup2                         = 3,
    COMPID_worldCup                          = 4,
    /* World Cup qualifiers: 8 group records (5..12) ... */
    COMPID_worldCupQualifyingGroup1          = 5,
    COMPID_worldCupQualifyingGroup2          = 6,
    COMPID_worldCupQualifyingGroup3          = 7,
    COMPID_worldCupQualifyingGroup4          = 8,
    COMPID_worldCupQualifyingGroup5          = 9,
    COMPID_worldCupQualifyingGroup6          = 10,
    COMPID_worldCupQualifyingGroup7          = 11,
    COMPID_worldCupQualifyingGroup8          = 12,
    /* ... + 5 regional qualifications (13..17). 5..17 is the WCQ range. */
    COMPID_northAmericaWorldCupQualification = 13,
    COMPID_southAmericaWorldCupQualification = 14,
    COMPID_africaWorldCupQualification       = 15,
    COMPID_asiaWorldCupQualification         = 16,
    COMPID_oceaniaWorldCupQualification      = 17,
    COMPID_invitationTournament              = 18,
    COMPID_tour                              = 19,
    COMPID_friendly                          = 20,
    COMPID_europeanChampionships2            = 21,
    COMPID_europeanChampionships             = 22,
    /* European Championship qualifiers: 8 group records (23..30). */
    COMPID_ecQualifyingGroup1                = 23,
    COMPID_ecQualifyingGroup2                = 24,
    COMPID_ecQualifyingGroup3                = 25,
    COMPID_ecQualifyingGroup4                = 26,
    COMPID_ecQualifyingGroup5                = 27,
    COMPID_ecQualifyingGroup6                = 28,
    COMPID_ecQualifyingGroup7                = 29,
    COMPID_ecQualifyingGroup8                = 30,
    COMPID_copaAmerica                       = 31,
    COMPID_africanNationsCup                 = 32,
    COMPID_northAmericanCup                  = 33,
    COMPID_asianCup                          = 34,
    COMPID_oceaniaCup                        = 35,
    COMPID_customsLeague                     = 36,
    COMPID_englishLeague                     = 46,
    /* English domestic cups (separate from their "_1" auto-advance aliases
     * 120/121). */
    COMPID_englishCup                        = 118,
    COMPID_englishLeagueCup                  = 119,
    /* Dynamic / data-only competitions start at 128 — the original SWOS comp ids
     * fill 0..127 exactly, so a brand-new competition (added as pure data in
     * tournaments.json with no compiled record) must use a byte >= 128. The
     * field is int8_t but every identity read casts (uint8_t), so 128 is read
     * back as 128 (see comp_id() below and docs/COMPETITION_DYNAMIC_REFACTOR.md
     * §8d). Widen Competition.compId to uint8_t before the Amiga bake. */
    COMPID_copaLibertadores                  = 128,
    /* The 36-team european PRESET substitutes (src/champions36.c): tournament
     * records played over the legacy euroCup/uefaCup/cupWinnersCup presets.
     * The ids mark the DIY buffers (field_2E) so the modern-format hooks fire
     * only for these — never for the career euro cups, which keep compIds
     * 0/1/2 and the legacy formats. Keep the three CONTIGUOUS:
     * Champions36_Is() tests the range. */
    COMPID_championsLeague                   = 129,
    COMPID_europaLeague                      = 130,
    COMPID_conferenceLeague                  = 131,
};

/* Read a competition's id (compId = byte 0 of the record). Endian-neutral
 * (single byte). `comp` must be non-NULL — callers that may pass NULL guard it
 * first (NULL was always a distinct, non-competition case in the asm). */
static inline uint8_t comp_id(const void *comp)
{
    return ((const uint8_t *)comp)[0];
}

/* World Cup qualifier set: the 8 group records (5..12) plus the 5 regional
 * qualifications (13..17). Replaces the *Copy address-range gate
 * `comp >= _worldCupQualifyingGroup1Copy && comp <= _oceaniaWorldCupQualificationCopy`
 * (the copies are laid out so that span = exactly compId 5..17). */
static inline int comp_is_wc_qualifier(uint8_t id)
{
    return id >= COMPID_worldCupQualifyingGroup1 &&
           id <= COMPID_oceaniaWorldCupQualification;
}

/* European Championship qualifier set: ec groups 1..8 (23..30). Replaces the
 * *Copy address-range gate
 * `comp >= _ecQualifyingGroup1Copy && comp <= _ecQualifyingGroup8Copy`. */
static inline int comp_is_ec_qualifier(uint8_t id)
{
    return id >= COMPID_ecQualifyingGroup1 &&
           id <= COMPID_ecQualifyingGroup8;
}

/* The two non-competitive exhibition formats (Tour, Friendly) — grouped
 * repeatedly (menus hide both; trophy/weight code treats them specially). */
static inline int comp_is_tour_or_friendly(uint8_t id)
{
    return id == COMPID_tour || id == COMPID_friendly;
}

/* Type-0 (competitionType==0) is OVERLOADED: it means "league FORMAT" (round-robin
 * table), used by BOTH real CompetitionLeague records (the country leagues, compId
 * >= COMPID_customsLeague == 36) AND several physically-CompetitionCup records that
 * play a group table — the WC qualifiers (5..17), the three exhibition comps
 * invitation/tour/friendly (18..20), the EC qualifiers (23..30), and the Oceania Cup
 * (35). Those cups store name[] at the CUP offset (20) with the embedded teamSeedList
 * right after, NOT the league layout (name[] at 38). So a type==0-means-league
 * assumption (comp_name_table) must route EVERY type-0 comp below the first league to
 * the cup name field, else name[] reads out of teamSeedList — best case it's NULL and
 * the caller substitutes a default division name (the Oceania Cup showed "PREMIER
 * DIVISION" via _divisionStringTable[0]); worst case it's a team-id-as-pointer once a
 * national-coach season has filled the draw list -> StringCopy SIGSEGV. The compId
 * order is fixed SWOS data: all type-0 cups sort below COMPID_customsLeague.
 * (Was `>= COMPID_invitationTournament && <= COMPID_friendly`, i.e. only 18..20 —
 * which missed the Oceania Cup and the WC/EC qualifiers.) */
static inline int comp_is_type0_cup(uint8_t id)
{
    return id < COMPID_customsLeague;
}

/* The six continental national-team cups — European Championship (the "2"
 * variant), Copa America, African Nations, North American, Asian, Oceania.
 * This is the "_wonContinentalCup" / major-continental set checked repeatedly
 * across career_status.c. Note: NOT contiguous (21, then 31..35). */
static inline int comp_is_continental_cup(uint8_t id)
{
    return id == COMPID_europeanChampionships2 ||
           id == COMPID_copaAmerica ||
           id == COMPID_africanNationsCup ||
           id == COMPID_northAmericanCup ||
           id == COMPID_asianCup ||
           id == COMPID_oceaniaCup;
}

#endif /* COMP_IDENTITY_H */
