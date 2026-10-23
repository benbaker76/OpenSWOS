#!/usr/bin/env python3
"""Friendly (match.active) planning — delivery step 4.

Flow (identified live on SDL, 2026-10-23, and declared in swos_screens):
  mainMenu -> FriendlyMenuInit -> OK -> CHOOSE TEAMS -> ChooseTeamsInit
  -> NATIONAL TEAMS | CLUB TEAMS -> SelectTeamsInit picker hops
  -> ChooseTeamsFriendlyInitFinish (team list; fire on a team row CYCLES its
     control: unselected -> COMPUTER(1) -> PLAYER-COACH(2) -> COACH(3) -> 0)
  -> PLAY FRIENDLY -> PlayMatchMenuInit (squad; PlayMatchSelected gates on
     the RAW _pl1Fire in the SAME frame as the menu fire)
  -> GameLoop_Frame (the match boundary, installed BEFORE the final press).

Control setting is EFFECT-AWARE (the doc's rule): read the actual control
from _selectedTeams, pulse once, re-read — never a blind count of fires.
SWOS can swap friendly teams, so the final match assertion is orientation-
agnostic and reports which side each team ended on.
"""
from testing_support import swos_screens as S
from testing_support.catalog import Catalog, normalize
from testing_support.executor import StepFailure, find_label
from testing_support.goals import (Goal, GoalError, S_ASSERTION, S_TIMEOUT,
                                   S_UNSUPPORTED)

CONTROL_VALUE = {"player": 2, "cpu": 1}       # TEAM_PLAYER_COACH / TEAM_COMPUTER
CONTROL_CYCLE_MAX = 5                         # 0->1->2->3->0 is 4 states


def _set_team_control(team_name: str, target: int):
    """Cycle one team row's control to `target`, reading the ACTUAL control
    from _selectedTeams between single pulses."""
    def fn(adapter, snap):
        def current():
            for t in adapter.read_selected_teams():
                if normalize(t["teamName"]) == normalize(team_name):
                    return t["teamControls"]
            return 0                            # not selected yet
        row, cands = find_label(snap, team_name)
        if row is None:
            raise StepFailure(
                "failed_precondition",
                f"team row {team_name!r} did not resolve to exactly one "
                f"visible entry ({len(cands)})",
                {"visible": [(e.ordinal, e.label) for e in snap.entries
                             if e.label][:40]})
        for _ in range(CONTROL_CYCLE_MAX):
            if current() == target:
                return
            adapter.activate(row.ordinal)
        if current() != target:
            raise StepFailure(S_TIMEOUT,
                              f"could not cycle {team_name} to control "
                              f"{target} in {CONTROL_CYCLE_MAX} pulses",
                              {"selected": adapter.read_selected_teams()})
    return fn


def _verify_selection(expected):
    """Exactly the requested teams, with the requested controls."""
    def fn(adapter, snap):
        got = {normalize(t["teamName"]): t["teamControls"]
               for t in adapter.read_selected_teams()}
        want = {normalize(n): c for n, c in expected}
        if got != want:
            raise StepFailure(S_ASSERTION,
                              f"selected teams {got} != requested {want}",
                              {"selected": adapter.read_selected_teams()})
    return fn


def _start_match(expected):
    """Install the GameLoop_Frame boundary, fire PLAY/COACH MATCH with the
    raw _pl1Fire in the same frame, and verify the match ACTUALLY holds the
    requested teams with the requested ownership (orientation-agnostic)."""
    def fn(adapter, snap):
        from testing_support.executor import find_callback
        row, _ = find_callback(snap, "PlayMatchSelected")
        if row is None:
            raise StepFailure("failed_precondition",
                              "PLAY/COACH MATCH entry not visible",
                              {"screen": snap.screen})
        teams = adapter.enter_match(row.ordinal)
        if teams is None:
            raise StepFailure(S_TIMEOUT,
                              "GameLoop_Frame was not reached after the "
                              "squad-screen fire")
        got = {normalize(t["teamName"]): t for t in teams}
        for name, control in expected:
            t = got.get(normalize(name))
            if t is None:
                raise StepFailure(S_ASSERTION,
                                  f"{name} is not in the match",
                                  {"match_teams": teams})
            owned = t["owner"] != 0
            if owned != (control == CONTROL_VALUE["player"]):
                raise StepFailure(S_ASSERTION,
                                  f"{name}: requested "
                                  f"{'player' if control == 2 else 'cpu'} but "
                                  f"match owner is {t['owner']}",
                                  {"match_teams": teams})
        adapter.verified_match = {"teams": teams,
                                  "orientation": [t["teamName"] for t in teams]}
    return fn


def plan_friendly(goal: Goal, catalog: Catalog, graph=None) -> dict:
    resolved = []
    for p in goal.participants:
        r = catalog.resolve_team(p.team.name, kind=p.team.kind,
                                 country=p.team.country)
        resolved.append((p, r))

    kinds = {r.kind for _, r in resolved}
    if len(kinds) != 1:
        raise GoalError(S_UNSUPPORTED,
                        "mixed club/national friendlies need modeled picker "
                        "exits (deferred capability)")
    kind = kinds.pop()
    if kind == "national":
        paths = {r.continent_label for _, r in resolved}
        kind_btn = "NationalTeams"
        hops = [resolved[0][1].continent_label]
    else:
        paths = {(r.continent_label, r.country_label, r.division_label)
                 for _, r in resolved}
        kind_btn = "ClubTeams"
        r0 = resolved[0][1]
        hops = [r0.continent_label, r0.country_label]
    if len(paths) != 1:
        raise GoalError(S_UNSUPPORTED,
                        "participants live on different picker paths "
                        f"({paths}); modeled picker exits are a deferred "
                        "capability")

    steps = [
        {"name": "open friendly", "precondition": {"screen_is": S.MAIN_MENU},
         "action": {"type": "activate_callback", "symbol": "FriendlyMenu"},
         "completion": {"screen_is": S.FRIENDLY_MENU}},
        {"name": "friendly OK",
         "action": {"type": "activate_callback", "symbol": "FriendlyOK"},
         "completion": {"label_visible": "CHOOSE TEAMS"},
         "skip_if": {"label_visible": "CHOOSE TEAMS"}},
        {"name": "choose teams",
         "action": {"type": "activate_callback", "symbol": "ChooseTeamsMenu"},
         "completion": {"screen_is": S.CHOOSE_KIND}},
        {"name": f"kind: {kind}",
         "action": {"type": "activate_callback", "symbol": kind_btn},
         "completion": {"screen_is": S.TEAM_PICKER}},
    ]
    for i, label in enumerate(hops):
        last = (i == len(hops) - 1 and kind == "national")
        steps.append({
            "name": f"picker hop {label}",
            "action": {"type": "activate_label", "label": label},
            "completion": ({"screen_is": S.FRIENDLY_TEAMLIST} if last
                           else {"rows_or_screen_changed": True})})
    if kind == "club":
        r0 = resolved[0][1]
        if r0.division_label is None:
            raise GoalError(S_UNSUPPORTED,
                            "no offline division label; runtime division "
                            "discovery is deferred")
        steps.append({
            "name": "picker hop (division)",
            "action": {"type": "activate_label", "label": r0.division_label},
            "skip_if": {"screen_is": S.FRIENDLY_TEAMLIST},
            "completion": {"screen_is": S.FRIENDLY_TEAMLIST}})

    expected = [(r.name, CONTROL_VALUE[p.control]) for p, r in resolved]
    for p, r in resolved:
        steps.append({
            "name": f"set {r.name} control to {p.control}",
            "action": {"type": "custom",
                       "desc": f"cycle {r.name} -> {p.control}",
                       "fn": _set_team_control(r.name, CONTROL_VALUE[p.control])},
            "precondition": {"screen_is": S.FRIENDLY_TEAMLIST},
            "completion": {}, "max_attempts": 1, "safe_retry": False})
    steps.append({
        "name": "verify selection",
        "action": {"type": "custom", "desc": "exactly the requested teams",
                   "fn": _verify_selection(expected)},
        "completion": {}, "max_attempts": 1})
    steps.append({
        "name": "play friendly",
        "action": {"type": "activate_callback",
                   "symbol": "ChooseTeamsPlayMatchSelected"},
        "completion": {"screen_is": S.PLAY_MATCH}, "timeout": 60})
    steps.append({
        "name": "start match (GameLoop_Frame boundary)",
        "action": {"type": "custom", "desc": "menu fire + raw _pl1Fire, "
                   "break GameLoop_Frame, verify teams/ownership",
                   "fn": _start_match(expected)},
        "precondition": {"screen_is": S.PLAY_MATCH},
        "completion": {}, "max_attempts": 1, "safe_retry": False,
        "timeout": 120})
    return {"goal": goal.to_dict(),
            "resolved": {"participants": [r.to_dict() for _, r in resolved]},
            "deferred": [], "steps": steps}
