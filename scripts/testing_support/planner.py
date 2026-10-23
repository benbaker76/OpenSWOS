#!/usr/bin/env python3
"""Goal -> explicit plan over the screen graph (docs/TESTING_FLOWS_DESIGN.md).

Plans are explicit compositions of graph edges with resolved parameters —
no search engine. Planning may observe but never acts; entity resolution is
offline (catalog) and every hop is verified online by the executor's
completion predicates. Arsenal and Partizan produce the SAME plan shape with
different parameters.
"""
from typing import Optional

from testing_support import swos_screens as S
from testing_support.catalog import Catalog, ResolvedTeam, normalize
from testing_support.executor import StepFailure
from testing_support.goals import (Goal, GoalError, S_ASSERTION,
                                   S_PRECONDITION, S_UNSUPPORTED)

INIT_CAREER_TIMEOUT = 150    # the club press runs InitCareer (slow)


def _edge_step(edge, **params):
    action = {k: (params[v[1:]] if isinstance(v, str) and v.startswith("$")
                  else v)
              for k, v in edge.action.items()}
    return {"name": edge.name, "action": action,
            "completion": dict(edge.completion),
            "timeout": edge.timeout, "max_attempts": edge.max_attempts,
            "safe_retry": edge.safe_retry}


def _career_club_assert(club: ResolvedTeam):
    """Completion evidence the doc demands: the career hub is reached AND the
    actual career club identity matches — read from _careerTeam, not inferred
    from the presses."""
    def fn(adapter, snap):
        got = adapter.read_career_team()
        if (normalize(got.get("teamName", "")) != normalize(club.name)
                or got.get("countryNumber") != club.country_number):
            raise StepFailure(
                S_ASSERTION,
                f"career club is {got} — expected {club.name!r} "
                f"(country {club.country_number})",
                {"expected": club.to_dict(), "actual": got})
        adapter.last_career_team = got
    return fn


def plan_career_new(goal: Goal, catalog: Catalog, graph=None) -> dict:
    graph = graph or S.build_graph()
    club = catalog.resolve_team(goal.club.name, kind=goal.club.kind or "club",
                                country=goal.club.country)
    if club.kind != "club":
        raise GoalError(S_UNSUPPORTED,
                        f"{club.name} is a national team; a career needs a club")

    path = graph.find_path("mainMenu", "teamPicker")
    if path is None:
        raise GoalError(S_UNSUPPORTED, "no declared route mainMenu->teamPicker")
    steps = [dict(_edge_step(e),
                  precondition=({"screen_is": graph.screens[e.src].init}
                                if i == 0 else None))
             for i, e in enumerate(path)]

    hop = next(e for e in graph.edges if e.name == "picker hop")
    deferred = []
    # continent, country — plain hops; completion: list or screen changed.
    for label in (club.continent_label, club.country_label):
        steps.append(_edge_step(hop, label=label))
    # division — skipped when the club is already visible; its completion IS
    # the online verification of the catalog's division prediction.
    if club.division_label is None:
        raise GoalError(
            S_UNSUPPORTED,
            f"no offline division label for {club.name} (country "
            f"{club.country_number}, division {club.division_index}); "
            "runtime division discovery is a deferred capability")
    div = _edge_step(hop, label=club.division_label)
    div["name"] = "picker hop (division)"
    div["skip_if"] = {"label_visible": club.name}
    div["completion"] = {"label_visible": club.name}
    steps.append(div)
    # the club press: InitCareer runs, then chairman memo(s) or the hub.
    final = _edge_step(hop, label=club.name)
    final["name"] = "pick club (InitCareer)"
    final["timeout"] = INIT_CAREER_TIMEOUT
    final["completion"] = {"screen_in": [S.CHAIRMAN_MEMO, S.CAREER_HUB]}
    steps.append(final)
    steps.append({
        "name": "dismiss welcome memos",
        "action": {"type": "dismiss_memo"},
        "skip_if": {"screen_is": S.CAREER_HUB},
        "precondition": {"screen_is": S.CHAIRMAN_MEMO},
        "completion": {"screen_is": S.CAREER_HUB},
        "retry_while_incomplete": True, "max_attempts": 8, "timeout": 30,
    })
    steps.append({
        "name": "verify career club identity",
        "action": {"type": "custom", "desc": "read _careerTeam, compare to goal",
                   "fn": _career_club_assert(club)},
        "completion": {"screen_is": S.CAREER_HUB},
        "max_attempts": 1,
    })
    return {"goal": goal.to_dict(), "resolved": {"club": club.to_dict()},
            "deferred": deferred, "steps": steps}


def plan_goal(goal: Goal, catalog: Catalog, graph=None,
              snapshot=None) -> dict:
    """Entry point. `snapshot` (optional, read-only) lets the planner check
    the starting screen; execution re-checks it anyway via preconditions."""
    if goal.goal == "career.active":
        p = plan_career_new(goal, catalog, graph)
    elif goal.goal == "match.active":
        from testing_support.friendly import plan_friendly
        p = plan_friendly(goal, catalog, graph)
    elif goal.goal == "season.active":
        from testing_support.season import plan_season
        p = plan_season(goal, catalog, graph)
    elif goal.goal == "competition.active":
        from testing_support.season import plan_preset_competition
        p = plan_preset_competition(goal, catalog, graph)
    else:
        raise GoalError(S_UNSUPPORTED, f"goal {goal.goal!r} not implemented")
    if snapshot is not None and snapshot.screen not in (None, S.MAIN_MENU):
        p["warnings"] = [f"target is at {snapshot.screen}, plan starts at the "
                         "main menu — the first precondition will fail unless "
                         "it is returned there"]
    p["catalog_digest"] = catalog.digest
    return p
