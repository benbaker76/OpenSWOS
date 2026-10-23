#!/usr/bin/env python3
"""Season (season.active) planning — a league season with one chosen team.

Flow (identified live on FS-UAE, 2026-10-23):
  mainMenu -> SEASON (SeasonMenu) -> SelectTeamsInit picker ("CHOOSE SEASON")
  -> continent -> country -> [division when the country has several]
  -> ChooseTeamsFriendlyInit: the WHOLE league pre-selected as COMPUTER(1),
     fire on a team row cycles its control (same cycle as friendlies)
  -> PLAY SEASON -> ContestMenuInit hub with _gameType == 3 (GAME_TYPE_SEASON).

Selecting a one-division country SKIPS the division level (YUGOSLAVIA lands
straight on the team list), so the division hop is skip_if-guarded.
"""
from testing_support import swos_screens as S
from testing_support.catalog import Catalog, normalize
from testing_support.executor import StepFailure
from testing_support.friendly import CONTROL_VALUE, _set_team_control
from testing_support.goals import (Goal, GoalError, S_ASSERTION,
                                   S_UNSUPPORTED)

GAME_TYPE_SEASON = 3    # include/play_match_menu.h
GAME_TYPE_PRESET = 2    # GAME_TYPE_PRESET_COMPETITION (src/file_io.c:443)


def _verify_season(team_name: str, target_control: int,
                   expect_game_type=GAME_TYPE_SEASON):
    """The hub is reached AND the chosen team actually carries the requested
    control in the selected-teams records; gameType is asserted when the
    capability has pinned it, recorded otherwise."""
    def fn(adapter, snap):
        if (expect_game_type is not None
                and snap.domain.get("gameType") != expect_game_type):
            raise StepFailure(S_ASSERTION,
                              f"gameType is {snap.domain.get('gameType')}, "
                              f"expected {expect_game_type}")
        teams = adapter.read_selected_teams()
        mine = [t for t in teams
                if normalize(t["teamName"]) == normalize(team_name)]
        if not mine or mine[0]["teamControls"] != target_control:
            raise StepFailure(S_ASSERTION,
                              f"{team_name}: control "
                              f"{mine[0]['teamControls'] if mine else 'absent'}"
                              f" != requested {target_control}",
                              {"selected": teams})
        adapter.verified_season = {
            "team": mine[0],
            "league_teams": len(teams),
            "gameType": snap.domain.get("gameType"),
        }
    return fn


def _plan_competition_like(goal: Goal, catalog: Catalog, *,
                           entry_symbol: str, entry_name: str,
                           comp_label, play_label: str,
                           expect_game_type, hub_screen=S.CAREER_HUB) -> dict:
    """Shared shape of SEASON and PRESET COMPETITION: picker to a country,
    fire a competition row, cycle the chosen team's control in the
    pre-selected entry list, press the PLAY row, verify at the hub."""
    r = catalog.resolve_team(goal.team.name, kind=goal.team.kind or "club",
                             country=goal.team.country)
    if r.kind != "club":
        raise GoalError(S_UNSUPPORTED,
                        f"{r.name} is a national team; {entry_name} needs a club")
    target = CONTROL_VALUE[goal.control]
    if comp_label is None:
        raise GoalError(S_UNSUPPORTED,
                        "no offline competition label; runtime discovery "
                        "is deferred")

    steps = [
        {"name": f"open {entry_name} picker",
         "precondition": {"screen_is": S.MAIN_MENU},
         "action": {"type": "activate_callback", "symbol": entry_symbol},
         "completion": {"screen_is": S.TEAM_PICKER}},
        {"name": f"picker hop {r.continent_label}",
         "action": {"type": "activate_label", "label": r.continent_label},
         "completion": {"rows_or_screen_changed": True}},
        {"name": f"picker hop {r.country_label}",
         "action": {"type": "activate_label", "label": r.country_label},
         "completion": {"rows_or_screen_changed": True}},
        {"name": f"pick competition {comp_label}",
         "action": {"type": "activate_label", "label": comp_label},
         "skip_if": {"screen_is": S.SEASON_TEAMLIST},
         "completion": {"screen_is": S.SEASON_TEAMLIST}},
        {"name": f"set {r.name} control to {goal.control}",
         "action": {"type": "custom",
                    "desc": f"cycle {r.name} -> {goal.control}",
                    "fn": _set_team_control(r.name, target)},
         "precondition": {"screen_is": S.SEASON_TEAMLIST},
         "completion": {}, "max_attempts": 1, "safe_retry": False},
        {"name": f"press {play_label}",
         "action": {"type": "activate_label", "label": play_label},
         "completion": {"screen_is": hub_screen},
         "timeout": 90},
        {"name": "verify competition team + control",
         "action": {"type": "custom", "desc": "gameType + selected control",
                    "fn": _verify_season(r.name, target, expect_game_type)},
         "completion": {}, "max_attempts": 1},
    ]
    return {"goal": goal.to_dict(),
            "resolved": {"team": r.to_dict(), "competition": comp_label},
            "deferred": [], "steps": steps}


def plan_season(goal: Goal, catalog: Catalog, graph=None) -> dict:
    r = catalog.resolve_team(goal.team.name, kind=goal.team.kind or "club",
                             country=goal.team.country)
    return _plan_competition_like(
        goal, catalog, entry_symbol="SeasonMenu", entry_name="season",
        comp_label=r.division_label, play_label="PLAY SEASON",
        expect_game_type=GAME_TYPE_SEASON)


def plan_preset_competition(goal: Goal, catalog: Catalog, graph=None) -> dict:
    """PRESET COMPETITION: the country's competitions (divisions + cups) are
    the picker rows; 'division N' resolves to its offline label (PRIMERA
    DIVISION etc.), an explicit 'label' names a cup or anything else."""
    comp = goal.competition_ref or {}
    country = comp.get("country") or goal.team.country
    label = comp.get("label")
    if label is None:
        cnum = catalog.country_number(country or "")
        if cnum is None:
            raise GoalError(S_UNSUPPORTED,
                            f"competition country {country!r} not resolvable")
        label = catalog.division_label(cnum, comp.get("division", 0))
    # The flows DIVERGE after the play press: SEASON lands on the
    # ContestMenuInit hub, a league PRESET lands on the league TABLE
    # (DIYLeagueTournamentInit, gameType 2) — both observed live. Only the
    # league-preset route is wired; cups/World Cup have their own screens
    # past this point and stay a reported capability gap.
    return _plan_competition_like(
        goal, catalog, entry_symbol="PresetCompetitionMenu",
        entry_name="preset competition", comp_label=label,
        play_label="PLAY LEAGUE", expect_game_type=GAME_TYPE_PRESET,
        hub_screen="DIYLeagueTournamentInit")
