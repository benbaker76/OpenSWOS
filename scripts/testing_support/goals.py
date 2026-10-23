#!/usr/bin/env python3
"""Typed goal schema + run statuses (docs/TESTING_FLOWS_DESIGN.md,
"Intent interpretation"). PURE — no I/O.

A goal is versioned JSON. The coding agent is the conversational
interpreter; this module only validates structure and applies the documented
defaults (recording them, so the plan/report can show what was assumed).
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional

SCHEMA_VERSION = 1

# Run dispositions (the report's `status`).
S_SUCCESS = "success"
S_CLARIFY = "clarification_needed"
S_UNSUPPORTED = "unsupported_capability"
S_NOT_FOUND = "entity_not_found"
S_PRECONDITION = "failed_precondition"
S_ASSERTION = "failed_assertion"
S_TIMEOUT = "timeout"
S_TRANSPORT = "transport_failure"
S_UNKNOWN = "unknown_outcome"

GOALS = ("career.active", "match.active", "season.active",
         "competition.active")
CONTROLS = ("player", "cpu")          # player => player-coach; cpu => computer
TEAM_KINDS = ("club", "national")


class GoalError(Exception):
    def __init__(self, status: str, detail: str, questions: Optional[List[str]] = None):
        super().__init__(detail)
        self.status = status
        self.detail = detail
        self.questions = questions or []

    def to_dict(self):
        return {"status": self.status, "detail": self.detail,
                "questions": self.questions}


@dataclass
class TeamRef:
    name: str
    kind: Optional[str] = None        # club | national | None (unconstrained)
    country: Optional[str] = None


@dataclass
class Participant:
    team: TeamRef
    control: str                      # player | cpu


@dataclass
class Goal:
    goal: str
    schema_version: int = SCHEMA_VERSION
    # career.active
    club: Optional[TeamRef] = None
    source: Optional[str] = None      # new | load | continue
    # match.active
    competition: Optional[str] = None
    participants: List[Participant] = field(default_factory=list)
    # season.active / competition.active
    team: Optional[TeamRef] = None
    control: Optional[str] = None
    # competition.active: {"country": ..., "division": N} or {"label": ...}
    competition_ref: Optional[dict] = None
    defaults_applied: Dict[str, str] = field(default_factory=dict)
    raw: Optional[dict] = None

    def to_dict(self):
        d = {"schema_version": self.schema_version, "goal": self.goal,
             "defaults_applied": dict(self.defaults_applied)}
        if self.goal == "career.active":
            d["club"] = dict(self.club.__dict__)
            d["source"] = self.source
        elif self.goal in ("season.active", "competition.active"):
            d["team"] = dict(self.team.__dict__)
            d["control"] = self.control
            if self.competition_ref is not None:
                d["competition"] = dict(self.competition_ref)
        else:
            d["competition"] = self.competition
            d["participants"] = [
                {"team": dict(p.team.__dict__), "control": p.control}
                for p in self.participants]
        return d


def _team_ref(data, where) -> TeamRef:
    if not isinstance(data, dict) or not data.get("name"):
        raise GoalError(S_CLARIFY, f"{where}: a team needs at least a name",
                        [f"Which team is meant at {where}?"])
    kind = data.get("kind")
    if kind is not None and kind not in TEAM_KINDS:
        raise GoalError(S_CLARIFY,
                        f"{where}: kind must be one of {TEAM_KINDS}, got {kind!r}")
    return TeamRef(name=str(data["name"]), kind=kind,
                   country=data.get("country"))


def parse_goal(data: dict) -> Goal:
    """Validate a goal dict. Raises GoalError with a structured status
    (clarification_needed / unsupported_capability) rather than guessing."""
    if not isinstance(data, dict):
        raise GoalError(S_CLARIFY, "goal must be a JSON object")
    ver = data.get("schema_version")
    if ver != SCHEMA_VERSION:
        raise GoalError(S_UNSUPPORTED,
                        f"schema_version {ver!r} unsupported (expected {SCHEMA_VERSION})")
    name = data.get("goal")
    if name not in GOALS:
        raise GoalError(S_UNSUPPORTED,
                        f"goal {name!r} unsupported; supported: {GOALS}")
    c = data.get("constraints") or {}
    g = Goal(goal=name, raw=data)

    if name == "career.active":
        g.club = _team_ref(c.get("club"), "constraints.club")
        src = c.get("source")
        if src is None:
            # Documented default: in a fresh main-menu context a club-specific
            # career request creates a new career. Recorded, not silent.
            src = "new"
            g.defaults_applied["source"] = "new (club-specific request defaults to a new career)"
        if src not in ("new", "load", "continue"):
            raise GoalError(S_CLARIFY,
                            f"source must be new/load/continue, got {src!r}")
        if src != "new":
            raise GoalError(S_UNSUPPORTED,
                            f"career source {src!r} is a later capability; "
                            "'new' is implemented")
        g.source = src
        return g

    if name in ("season.active", "competition.active"):
        # A league/competition with one chosen team; the rest of the entry
        # list is pre-selected as computer teams by the game itself.
        g.team = _team_ref(c.get("team"), "constraints.team")
        ctl = c.get("control")
        if ctl is None:
            ctl = "player"
            g.defaults_applied["control"] = \
                "player (defaults to player-coaching the chosen team)"
        if ctl not in CONTROLS:
            raise GoalError(S_CLARIFY,
                            f"control must be one of {CONTROLS}, got {ctl!r}")
        g.control = ctl
        if name == "competition.active":
            comp = c.get("competition")
            if not isinstance(comp, dict) or not (
                    "label" in comp or "division" in comp):
                raise GoalError(
                    S_CLARIFY,
                    "competition.active needs constraints.competition with "
                    "either {'division': N[, 'country': ...]} or "
                    "{'label': '<picker label>'}",
                    ["Which competition? e.g. {'country': 'Spain', "
                     "'division': 0} for the first league"])
            if "division" in comp and not isinstance(comp["division"], int):
                raise GoalError(S_CLARIFY, "competition.division must be an "
                                           "integer index (0 = first league)")
            g.competition_ref = dict(comp)
        return g

    # match.active
    comp = c.get("competition")
    if comp != "friendly":
        raise GoalError(S_UNSUPPORTED,
                        f"competition {comp!r} unsupported for match.active; "
                        "'friendly' is implemented")
    g.competition = comp
    parts = c.get("participants")
    if not isinstance(parts, list) or len(parts) != 2:
        raise GoalError(S_CLARIFY,
                        "a friendly needs exactly 2 participants",
                        ["Which two teams play, and who controls each?"])
    for i, p in enumerate(parts):
        ctl = (p or {}).get("control")
        if ctl not in CONTROLS:
            raise GoalError(S_CLARIFY,
                            f"participants[{i}].control must be one of {CONTROLS}")
        g.participants.append(
            Participant(team=_team_ref((p or {}).get("team"),
                                       f"participants[{i}].team"),
                        control=ctl))
    if "home_away" in c:
        raise GoalError(S_UNSUPPORTED,
                        "home/away constraint not implemented (SWOS may swap "
                        "friendly teams; orientation is reported, not forced)")
    return g
