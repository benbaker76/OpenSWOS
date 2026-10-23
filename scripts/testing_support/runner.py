#!/usr/bin/env python3
"""Request -> plan -> execution -> report (docs/TESTING_FLOWS_DESIGN.md,
"Interfaces and reports"). The CLI and the Python API share this module;
tests drive it with a fake adapter."""
import json
import time
from typing import List, Optional

from testing_support.catalog import load_catalog
from testing_support.executor import Executor
from testing_support.goals import GoalError, parse_goal
from testing_support.planner import plan_goal

REPORT_SCHEMA = 1


def _strip_fns(steps):
    return [{k: v for k, v in s.items() if k != "action"}
            | {"action": {k: v for k, v in s["action"].items() if k != "fn"}}
            for s in steps]


def make_plan(request: dict, catalog_paths: Optional[List[str]] = None,
              snapshot=None, catalog=None) -> dict:
    """Validate + resolve + plan. Observation-free unless a snapshot is
    handed in; NEVER acts (the doc's read-only planning rule)."""
    goal = parse_goal(request)
    cat = catalog or load_catalog(catalog_paths)
    plan = plan_goal(goal, cat, snapshot=snapshot)
    return plan


def run_request(request: dict, adapter, catalog_paths=None,
                catalog=None) -> dict:
    """Execute a request end to end; returns the full report. The target is
    left stopped at the final verified boundary."""
    t0 = time.monotonic()
    report = {"report_schema": REPORT_SCHEMA, "request": request,
              "status": None, "target_stopped": True}
    try:
        plan = make_plan(request, catalog_paths, catalog=catalog)
    except GoalError as e:
        report.update(e.to_dict())
        report["totals"] = {"elapsed_s": round(time.monotonic() - t0, 3)}
        return report
    report["goal"] = plan["goal"]
    report["resolved"] = plan.get("resolved", {})
    report["catalog_digest"] = plan.get("catalog_digest")
    report["plan"] = _strip_fns(plan["steps"])

    try:
        report["target_identity"] = adapter.identity()
    except Exception as e:                       # identity is metadata only
        report["target_identity"] = {"error": str(e)}

    body = Executor(adapter).run_plan(plan["steps"])
    report.update(body)
    if getattr(adapter, "last_career_team", None):
        report["verified_career_team"] = adapter.last_career_team
    if getattr(adapter, "verified_match", None):
        report["verified_match"] = adapter.verified_match
    if getattr(adapter, "verified_season", None):
        report["verified_season"] = adapter.verified_season
    report["totals"]["total_elapsed_s"] = round(time.monotonic() - t0, 3)
    return report


def save_report(report: dict, path: str):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
