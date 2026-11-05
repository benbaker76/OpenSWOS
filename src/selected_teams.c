/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 */
/* selected_teams.c — UNTANGLE step 3 (careerFileBuffer overlay retirement).
 *
 * The career-mode selected-team records lifted out of the careerFileBuffer
 * overlay into standalone storage. `selTeamsPtr` is NOT a pointer — it is the
 * base of an inline `TeamFileEx[]` array (684-byte stride); team `i` is
 * `(TeamFileEx *)&selTeamsPtr + i`. The five `selTeam*` labels are aliases into
 * record [0]'s TeamFile fields (teamControls/teamName/league/prSocksColor/
 * playerNumbers at +4/+5/+25/+30/+60) — code derives the per-record field offset
 * as `(&selTeamX - &selTeamsPtr)`. Max 32 teams (career_teams.c:262;
 * SAVE_SIZE 0x5582 = 2 + 32*684). `selectedTeams` is a typed alias at the same
 * base so new code can use named-field access.
 *
 * The run is ONE contiguous block (68400 B) because it is a load-bearing union:
 * the 32-team array (21888 B) overruns past `selTeamsPtr` into `editTeamsBuffer`
 * for teams 2..31, and career_teams.c:1660 stores selTeamsPtr-relative offsets
 * in competition-slot fields that reach into that space (see swos_globals.c:583).
 * editTeamsBuffer (edit-teams workspace) and editTeamsSaveVarsArea therefore
 * relocate together, keeping every relative offset intact. All of these are
 * &symbol-addressed and not saved (save_format_json.c:2558 drops the scratch
 * regions), so this is a routine format-neutral extraction.
 *
 * This was only safe AFTER:
 *   - phase 1 decoupled _Save/RestoreSelectedTeams from the numSelectedTeams->
 *     array adjacency (career_teams.c), and
 *   - the careerTeam step-4 rework removed the careerTeam adjacency below
 *     selTeamsPtr (diy_resolve_team identity branch). See
 *     docs/SELECTED_TEAMS_UNTANGLE.md.
 *
 * Save-format-neutral: the JSON save already serializes a typed `selected_teams`
 * array and uses identity-based teamSel. The legacy .CAR raw reader loses these
 * rosters (they land in the overlay's .skip hole) — an accepted degradation of
 * the best-effort legacy path; JSON is the format. Emitted as raw-byte labels so
 * the typed-but-undersized externs ((TeamFileEx *)&selTeamsPtr) resolve exactly
 * as when it lived in the overlay.
 *
 * Run layout (offsets from base) — must match the old overlay run exactly:
 *   +0      selectedTeams / selTeamsPtr  (TeamFileEx[] base)
 *   +4      selTeamIsComputerFlag        (TeamFile.teamControls)
 *   +5      selTeamNameBase              (TeamFile.teamName[17])
 *   +25     selTeamImportField           (TeamFile.league)
 *   +30     selTeamSocksColor            (TeamFile.prSocksColor)
 *   +60     selTeamPlayerIndices         (TeamFile.playerNumbers[16])
 *   +1368   editTeamsBuffer              (43778 B edit-teams workspace; union)
 *   +45146  editTeamsSaveVarsArea        (23254 B edit-teams save scratch)
 *   +68400  (end)
 */
#include "swos_types.h"
#include "swos_platform.h"
#include "game_types.h"   /* TeamFileEx + the selTeam / editTeams offset macros */

/* Standalone zero-init BSS storage — pure C (was an __asm__ .skip block;
 * converted for the WASM/Emscripten target, which rejects asm-defined data
 * symbols — see docs/WASM.md §4b).
 *
 * The whole 68400-byte run is ONE contiguous TeamFileEx[100] (100 * 684), a
 * load-bearing union: records [0..1] are the real selected teams; the 32-team
 * roster overruns from record [2] onward into what the Amiga overlay labelled
 * editTeamsBuffer (record[2], +1368) and editTeamsSaveVarsArea (+45146), and
 * career_teams.c stores selTeamsPtr-relative offsets that reach into that space.
 * Modelling it as the full array keeps every relative offset intact with no
 * out-of-bounds. The historical sub-labels (selTeamsPtr / selTeamNameBase /
 * selTeamImportField / selTeamPlayerIndices / editTeamsBuffer /
 * editTeamsSaveVarsArea) are now pure-C offset macros in game_types.h; the
 * asserts below lock their offsets to this layout. All of these are
 * &symbol-addressed and not byte-saved (the JSON save serialises a typed
 * selected_teams array by count + drops the scratch regions), so this is
 * format-neutral. */
TeamFileEx _selectedTeams[SELECTED_TEAMS_MAX];

_Static_assert(sizeof(_selectedTeams) == SELECTED_TEAMS_MAX * 684,
               "selectedTeams is SELECTED_TEAMS_MAX records of 684 bytes");
_Static_assert(SELECTED_TEAMS_MAX >= 100,
               "the scratch overlays above sit inside the original's 100 records (68400 bytes)");
_Static_assert(sizeof(TeamFileEx) == 684, "TeamFileEx stride must be 684");
/* Lock the historical sub-label offsets the game_types.h macros encode: */
_Static_assert(offsetof(TeamFileEx, team) == 0,                         "selTeamsPtr @ +0");
_Static_assert(offsetof(TeamFile, teamName)      ==  5,                 "selTeamNameBase @ +5");
_Static_assert(offsetof(TeamFile, league)        == 25,                 "selTeamImportField @ +25");
_Static_assert(offsetof(TeamFile, playerNumbers) == 60,                 "selTeamPlayerIndices @ +60");
_Static_assert(2 * sizeof(TeamFileEx)            == 1368,               "editTeamsBuffer @ +1368");
_Static_assert(1368 + 43778                      == 45146,             "editTeamsSaveVarsArea @ +45146");
